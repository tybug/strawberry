/* This file is part of Strawberry.
   Copyright 2011, David Sansome <me@davidsansome.com>
   Copyright 2018-2021, Jonas Kvinge <jonas@jkvinge.net>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "messagehandler.h"

#include <QObject>
#include <QAbstractSocket>
#include <QDataStream>
#include <QIODevice>
#include <QLocalSocket>
#include <QByteArray>

#include "core/logging.h"

_MessageHandlerBase::_MessageHandlerBase(QIODevice *device, QObject *parent)
    : QObject(parent),
      device_(nullptr),
      flush_abstract_socket_(nullptr),
      flush_local_socket_(nullptr),
      reading_protobuf_(false),
      expected_length_(0),
      is_device_closed_(false) {
  if (device) {
    SetDevice(device);
  }
}

void _MessageHandlerBase::SetDevice(QIODevice *device) {

  device_ = device;

  buffer_.open(QIODevice::ReadWrite);

  QObject::connect(device, &QIODevice::readyRead, this, &_MessageHandlerBase::DeviceReadyRead);

  // Yeah I know.
  if (QAbstractSocket *abstractsocket = qobject_cast<QAbstractSocket*>(device)) {
    flush_abstract_socket_ = &QAbstractSocket::flush;
    QObject::connect(abstractsocket, &QAbstractSocket::disconnected, this, &_MessageHandlerBase::DeviceClosed);
  }
  else if (QLocalSocket *localsocket = qobject_cast<QLocalSocket*>(device)) {
    flush_local_socket_ = &QLocalSocket::flush;
    QObject::connect(localsocket, &QLocalSocket::disconnected, this, &_MessageHandlerBase::DeviceClosed);
  }
  else {
    qFatal("Unsupported device type passed to _MessageHandlerBase");
  }

}

void _MessageHandlerBase::DeviceReadyRead() {

  while (device_->bytesAvailable() > 0) {
    if (!reading_protobuf_) {
      // Read the length of the next message
      QDataStream s(device_);
      s >> expected_length_;

      reading_protobuf_ = true;
    }

    // Read some of the message
    buffer_.write(device_->read(expected_length_ - buffer_.size()));

    // Did we get everything?
    if (buffer_.size() == expected_length_) {
      // Parse the message
      if (!RawMessageArrived(buffer_.data())) {
        qLog(Error) << "Malformed protobuf message";
        device_->close();
        return;
      }

      // Clear the buffer
      buffer_.close();
      buffer_.setData(QByteArray());
      buffer_.open(QIODevice::ReadWrite);
      reading_protobuf_ = false;
    }
  }

}

void _MessageHandlerBase::WriteMessage(const QByteArray &data) {

  QDataStream s(device_);
  s << quint32(data.length());
  s.writeRawData(data.data(), static_cast<int>(data.length()));

  // Sorry.
  if (flush_abstract_socket_) {
    ((qobject_cast<QAbstractSocket*>(device_))->*(flush_abstract_socket_))();
  }
  else if (flush_local_socket_) {
    ((qobject_cast<QLocalSocket*>(device_))->*(flush_local_socket_))();
  }

}

void _MessageHandlerBase::DeviceClosed() {
  is_device_closed_ = true;
  AbortAll();
}
