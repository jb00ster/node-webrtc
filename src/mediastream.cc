/* Copyright (c) 2018 The node-webrtc project authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE.md file in the root of the source tree. All contributing
 * project authors may be found in the AUTHORS file in the root of the source
 * tree.
 */
#include "src/mediastream.h"

#include "src/converters/arguments.h"
#include "src/converters/v8.h"
#include "src/converters/webrtc.h"

using node_webrtc::EventLoop;
using node_webrtc::MediaStream;
using node_webrtc::MediaStreamTrack;
using node_webrtc::PeerConnectionFactory;
using v8::External;
using v8::Function;
using v8::FunctionTemplate;
using v8::Handle;
using v8::Local;
using v8::Object;
using v8::Value;

Nan::Persistent<Function> MediaStream::constructor;

std::map<rtc::scoped_refptr<webrtc::MediaStreamInterface>, MediaStream*> MediaStream::_streams;

MediaStream::MediaStream(
    std::shared_ptr<node_webrtc::PeerConnectionFactory>&& factory,
    rtc::scoped_refptr<webrtc::MediaStreamInterface>&& stream)
  : Nan::AsyncResource("MediaStream")
  , _factory(std::move(factory))
  , _stream(std::move(stream)) {
  // Do nothing.
}

MediaStream::~MediaStream() {
  MediaStream::Release(this);
}

NAN_METHOD(MediaStream::New) {
  if (info.Length() != 2 || !info[0]->IsExternal() || !info[1]->IsExternal()) {
    return Nan::ThrowTypeError("You cannot construct a MediaStream");
  }

  auto factory = *static_cast<std::shared_ptr<node_webrtc::PeerConnectionFactory>*>(Local<External>::Cast(info[0])->Value());
  auto stream = *static_cast<rtc::scoped_refptr<webrtc::MediaStreamInterface>*>(Local<External>::Cast(info[1])->Value());

  auto obj = new MediaStream(std::move(factory), std::move(stream));
  obj->Wrap(info.This());
  obj->Ref();

  info.GetReturnValue().Set(info.This());
}

NAN_GETTER(MediaStream::GetId) {
  auto self = Nan::ObjectWrap::Unwrap<MediaStream>(info.Holder());
  info.GetReturnValue().Set(Nan::New(self->_stream->label()).ToLocalChecked());
}

NAN_GETTER(MediaStream::GetActive) {
  auto self = Nan::ObjectWrap::Unwrap<MediaStream>(info.Holder());
  auto active = false;
  for (auto const& track : self->_stream->GetAudioTracks()) {
    active = active ||
        track->state() == webrtc::MediaStreamTrackInterface::TrackState::kLive;
  }
  for (auto const& track : self->_stream->GetVideoTracks()) {
    active = active ||
        track->state() == webrtc::MediaStreamTrackInterface::TrackState::kLive;
  }
  info.GetReturnValue().Set(Nan::New(active));
}

NAN_METHOD(MediaStream::GetAudioTracks) {
  auto self = Nan::ObjectWrap::Unwrap<MediaStream>(info.Holder());
  auto tracks = std::vector<MediaStreamTrack*>();
  for (auto const& track : self->_stream->GetAudioTracks()) {
    auto mediaStreamTrack = MediaStreamTrack::GetOrCreate(self->_factory, track);
    tracks.push_back(mediaStreamTrack);
  }
  CONVERT_OR_THROW_AND_RETURN(tracks, result, Local<Value>);
  info.GetReturnValue().Set(result);
}

NAN_METHOD(MediaStream::GetVideoTracks) {
  auto self = Nan::ObjectWrap::Unwrap<MediaStream>(info.Holder());
  auto tracks = std::vector<MediaStreamTrack*>();
  for (auto const& track : self->_stream->GetVideoTracks()) {
    auto mediaStreamTrack = MediaStreamTrack::GetOrCreate(self->_factory, track);
    tracks.push_back(mediaStreamTrack);
  }
  CONVERT_OR_THROW_AND_RETURN(tracks, result, Local<Value>);
  info.GetReturnValue().Set(result);
}

NAN_METHOD(MediaStream::GetTracks) {
  auto self = Nan::ObjectWrap::Unwrap<MediaStream>(info.Holder());
  auto tracks = std::vector<MediaStreamTrack*>();
  for (auto const& track : self->_stream->GetAudioTracks()) {
    auto mediaStreamTrack = MediaStreamTrack::GetOrCreate(self->_factory, track);
    tracks.push_back(mediaStreamTrack);
  }
  for (auto const& track : self->_stream->GetVideoTracks()) {
    auto mediaStreamTrack = MediaStreamTrack::GetOrCreate(self->_factory, track);
    tracks.push_back(mediaStreamTrack);
  }
  CONVERT_OR_THROW_AND_RETURN(tracks, result, Local<Value>);
  info.GetReturnValue().Set(result);
}

NAN_METHOD(MediaStream::GetTrackById) {
  CONVERT_ARGS_OR_THROW_AND_RETURN(label, std::string);
  auto self = Nan::ObjectWrap::Unwrap<MediaStream>(info.Holder());
  auto audioTrack = self->_stream->FindAudioTrack(label);
  if (audioTrack) {
    auto track = MediaStreamTrack::GetOrCreate(self->_factory, audioTrack);
    info.GetReturnValue().Set(track->handle());
  }
  auto videoTrack = self->_stream->FindAudioTrack(label);
  if (videoTrack) {
    auto track = MediaStreamTrack::GetOrCreate(self->_factory, videoTrack);
    info.GetReturnValue().Set(track->handle());
  }
}

NAN_METHOD(MediaStream::AddTrack) {
  CONVERT_ARGS_OR_THROW_AND_RETURN(mediaStreamTrack, MediaStreamTrack*);
  auto self = Nan::ObjectWrap::Unwrap<MediaStream>(info.Holder());
  auto stream = self->_stream;
  auto track = mediaStreamTrack->track();
  if (track->kind() == track->kAudioKind) {
    stream->AddTrack(static_cast<webrtc::AudioTrackInterface*>(track.get()));
  } else {
    stream->AddTrack(static_cast<webrtc::VideoTrackInterface*>(track.get()));
  }
}

NAN_METHOD(MediaStream::RemoveTrack) {
  CONVERT_ARGS_OR_THROW_AND_RETURN(mediaStreamTrack, MediaStreamTrack*);
  auto self = Nan::ObjectWrap::Unwrap<MediaStream>(info.Holder());
  auto stream = self->_stream;
  auto track = mediaStreamTrack->track();
  if (track->kind() == track->kAudioKind) {
    stream->RemoveTrack(static_cast<webrtc::AudioTrackInterface*>(track.get()));
  } else {
    stream->RemoveTrack(static_cast<webrtc::VideoTrackInterface*>(track.get()));
  }
}

NAN_METHOD(MediaStream::Clone) {
  Nan::ThrowError("Not yet implemented; file a feature request against node-webrtc");
}

MediaStream* MediaStream::GetOrCreate(
    std::shared_ptr<PeerConnectionFactory> factory,
    rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
  Nan::HandleScope scope;
  if (_streams.count(stream)) {
    return _streams[stream];
  }
  Local<Value> cargv[2];
  cargv[0] = Nan::New<External>(static_cast<void*>(&factory));
  cargv[1] = Nan::New<External>(static_cast<void*>(&stream));
  auto mediaStream = Nan::ObjectWrap::Unwrap<MediaStream>(
          Nan::New(MediaStream::constructor)->NewInstance(2, cargv));
  _streams[stream] = mediaStream;
  return mediaStream;
}

void MediaStream::Release(MediaStream* stream) {
  // TODO(mroberts): Use some bidirectional map instead.
  for (auto pair : _streams) {
    if (pair.second == stream) {
      _streams.erase(pair.first);
      return;
    }
  }
}

void MediaStream::Init(Handle<Object> exports) {
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("MediaStream").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  Nan::SetAccessor(tpl->InstanceTemplate(), Nan::New("id").ToLocalChecked(), GetId, nullptr);
  Nan::SetAccessor(tpl->InstanceTemplate(), Nan::New("active").ToLocalChecked(), GetActive, nullptr);
  Nan::SetPrototypeMethod(tpl, "getAudioTracks", GetAudioTracks);
  Nan::SetPrototypeMethod(tpl, "getVideoTracks", GetVideoTracks);
  Nan::SetPrototypeMethod(tpl, "getTracks", GetTracks);
  Nan::SetPrototypeMethod(tpl, "getTrackById", GetTrackById);
  Nan::SetPrototypeMethod(tpl, "addTrack", AddTrack);
  Nan::SetPrototypeMethod(tpl, "removeTrack", RemoveTrack);
  Nan::SetPrototypeMethod(tpl, "clone", Clone);
  constructor.Reset(tpl->GetFunction());
  exports->Set(Nan::New("MediaStream").ToLocalChecked(), tpl->GetFunction());
}