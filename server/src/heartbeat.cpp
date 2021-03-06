/****************************************************************************************
**
** Copyright (C) 2014 Jolla Ltd.
** Contact: Simo Piiroinen <simo.piiroinen@jollamobile.com>
** All rights reserved.
**
** This file is part of nemo keepalive package.
**
** You may use this file under the terms of the GNU Lesser General
** Public License version 2.1 as published by the Free Software Foundation
** and appearing in the file license.lgpl included in the packaging
** of this file.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file license.lgpl included in the packaging
** of this file.
**
** This library is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
** Lesser General Public License for more details.
**
****************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <QtGlobal>

#include "heartbeat.h"

Heartbeat::Heartbeat(QObject *parent) : QObject(parent)
{
  m_started   = false;
  m_waiting   = false;

  m_min_delay = 0;
  m_max_delay = 0;

  m_iphb_handle     = 0;
  m_wakeup_notifier = 0;
  m_connect_timer   = new QTimer();
  QObject::connect(m_connect_timer, SIGNAL(timeout()),
                   this, SLOT(retryConnect()));
}

Heartbeat::~Heartbeat(void)
{
  disconnect();
  delete m_connect_timer;
}

bool
Heartbeat::tryConnect(void)
{
  bool   status = false;
  iphb_t handle = 0;

  if( !m_iphb_handle ) {
    int fd;

    if( !(handle = iphb_open(0)) ) {
      qWarning("iphb_open: %s", strerror(errno));
      goto cleanup;
    }

    if( (fd = iphb_get_fd(handle)) == -1 ) {
      qWarning("iphb_get_fd: %s", strerror(errno));
      goto cleanup;
    }

    m_iphb_handle = handle, handle = 0;
    m_wakeup_notifier = new QSocketNotifier(fd, QSocketNotifier::Read);
    QObject::connect(m_wakeup_notifier, SIGNAL(activated(int)),
                     this, SLOT(wakeup(int)));
    m_wakeup_notifier->setEnabled(true);
  }

  status = true;

cleanup:
  if( handle ) iphb_close(handle);

  return status;
}

void
Heartbeat::disconnect(void)
{
  stop();

  m_connect_timer->stop();

  if( m_wakeup_notifier ) {
    delete m_wakeup_notifier;
    m_wakeup_notifier = 0;
  }

  if( m_iphb_handle ) {
    iphb_close(m_iphb_handle);
    m_iphb_handle = 0;
  }
}

void
Heartbeat::retryConnect(void)
{
  if( tryConnect() ) {
    // cancel retry timer
    m_connect_timer->stop();
    // issue iphb wait
    wait();
  }
}

void
Heartbeat::connect(void)
{
  if( m_connect_timer->isActive() ) {
    // Retry timer already set up
  }
  else if( !tryConnect() ) {
    // Start retry timer
    m_connect_timer->setInterval(5 * 1000);
    m_connect_timer->start();
  }
}

void
Heartbeat::setInterval(int mindelay, int maxdelay)
{
  // TODO: anchor to monotime to preserve over reconnect
  m_min_delay = mindelay;
  m_max_delay = maxdelay;
}

void
Heartbeat::setInterval(int global_slot)
{
  setInterval(global_slot, global_slot);
}

void
Heartbeat::start(int global_slot)
{
  setInterval(global_slot), start();
}

void
Heartbeat::start(int mindelay, int maxdelay)
{
  setInterval(mindelay, maxdelay), start();
}

void
Heartbeat::start(void)
{
  m_started = true, wait();
}

void
Heartbeat::wait(void)
{
  if( !m_started ) {
    return;
  }

  if( m_waiting ) {
    return;
  }

  if( m_min_delay <= 0 ) {
    qWarning("missing heartbeat delay");
    return;
  }

  if( m_max_delay < m_min_delay ) {
    qWarning("invalid heartbeat delay");
    return;
  }

  if( !m_iphb_handle ) {
    connect();
  }

  if( !m_waiting && m_iphb_handle ) {
    int lo = m_min_delay;
    int hi = m_max_delay;
    if( lo != hi ) {
      // TODO: from monotime to relative
      // TODO: timeout if already passed
    }
    iphb_wait(m_iphb_handle, lo, hi, 0);
    m_waiting = true;
  }
}

void
Heartbeat::wakeup(int fd)
{
  bool keep_going = true;

  /* The data itself is not interesting, we just want to
   * know whether the read succeeded or not */
  char buf[256];
  int rc = read(fd, buf, sizeof buf);

  if( rc == -1 ) {
    switch( errno ) {
    case EAGAIN:
    case EINTR:
      // Recoverable errors -> ignored
      break;

    default:
      // Irrecoverable errors -> reset connection
      keep_going = false;
      break;
    }
  }
  else if( rc == 0 ) {
    // EOF -> assume dsme restart -> reset connection
    keep_going = false;
  }
  else {
    m_started = false;
    emit timeout();
  }

  m_waiting = false;

  if( !keep_going ) {
    // Terminate lost connection & start reconnect attempt
    disconnect();
    connect();
  }
}

void
Heartbeat::stop(void)
{
  if( m_waiting && m_iphb_handle ) {
    iphb_wait(m_iphb_handle, 0, 0, 0);
  }
  m_waiting = false;
  m_started = false;
}
