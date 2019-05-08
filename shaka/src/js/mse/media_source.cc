// Copyright 2016 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/js/mse/media_source.h"

#include <glog/logging.h>

#include <cmath>
#include <functional>
#include <utility>

#include "src/js/events/event.h"
#include "src/js/events/event_names.h"
#include "src/js/events/media_encrypted_event.h"
#include "src/js/mse/source_buffer.h"
#include "src/js/mse/video_element.h"
#include "src/media/media_utils.h"
#include "src/util/macros.h"

namespace shaka {
namespace js {
namespace mse {

namespace {

/** @returns A random blob URL using a randomly generated UUID. */
std::string RandomUrl() {
  unsigned char bytes[16];
  // Use pseudo-random since we don't need cryptographic security.
  for (unsigned char& chr : bytes)
    chr = static_cast<unsigned char>(rand());  // NOLINT

    // Since it's random, don't care about host byte order.
#define _2_BYTES_AT(b) (*reinterpret_cast<uint16_t*>(b))
#define _4_BYTES_AT(b) (*reinterpret_cast<uint32_t*>(b))

  return util::StringPrintf(
      "blob:%08x-%04x-%04x-%04x-%08x%04x", _4_BYTES_AT(bytes),
      _2_BYTES_AT(bytes + 4),
      // Only output 3 hex chars, the first is the UUID version (4, Random).
      (_2_BYTES_AT(bytes + 6) & 0xfff) | 0x4000,
      // Drop the two high bits to set the variant (0b10xx).
      (_2_BYTES_AT(bytes + 8) & 0x3fff) | 0x8000, _4_BYTES_AT(bytes + 10),
      _2_BYTES_AT(bytes + 14));
}

}  // namespace

using namespace std::placeholders;  // NOLINT

BEGIN_ALLOW_COMPLEX_STATICS
std::unordered_map<std::string, Member<MediaSource>>
    MediaSource::media_sources_;
END_ALLOW_COMPLEX_STATICS

MediaSource::MediaSource()
    : ready_state(MediaSourceReadyState::CLOSED),
      url(RandomUrl()),
      controller_(std::bind(&MediaSource::OnMediaError, this, _1, _2),
                  std::bind(&MediaSource::OnWaitingForKey, this),
                  std::bind(&MediaSource::OnEncrypted, this, _1, _2),
                  std::bind(&MediaSource::OnReadyStateChanged, this, _1),
                  std::bind(&MediaSource::OnPipelineStatusChanged, this, _1)) {
  AddListenerField(EventType::SourceOpen, &on_source_open);
  AddListenerField(EventType::SourceEnded, &on_source_ended);
  AddListenerField(EventType::SourceClose, &on_source_close);

  DCHECK_EQ(0u, media_sources_.count(url));
  media_sources_[url] = this;
}

// \cond Doxygen_Skip
MediaSource::~MediaSource() {
  DCHECK_EQ(1u, media_sources_.count(url));
  media_sources_.erase(url);
}
// \endcond Doxygen_Skip

// static
bool MediaSource::IsTypeSupported(const std::string& mime_type) {
  std::string container;
  std::string codec;
  media::SourceType source_type;
  return ParseMimeAndCheckSupported(mime_type, &source_type, &container,
                                    &codec);
}

// static
RefPtr<MediaSource> MediaSource::FindMediaSource(const std::string& url) {
  if (media_sources_.count(url) == 0)
    return nullptr;
  return media_sources_[url];
}

void MediaSource::Trace(memory::HeapTracer* tracer) const {
  EventTarget::Trace(tracer);
  for (auto& pair : source_buffers_)
    tracer->Trace(&pair.second);
  tracer->Trace(&video_element_);
}

ExceptionOr<RefPtr<SourceBuffer>> MediaSource::AddSourceBuffer(
    const std::string& type) {
  media::SourceType source_type;
  const media::Status status = controller_.AddSource(type, &source_type);
  if (status == media::Status::NotSupported) {
    return JsError::DOMException(
        NotSupportedError, "The given type ('" + type + "') is unsupported.");
  }
  if (status == media::Status::NotAllowed) {
    return JsError::DOMException(
        NotSupportedError, "Cannot add any additional SourceBuffer objects.");
  }

  CHECK(status == media::Status::Success);
  DCHECK_EQ(0u, source_buffers_.count(source_type));
  DCHECK_NE(source_type, media::SourceType::Unknown);
  return (source_buffers_[source_type] = new SourceBuffer(this, source_type));
}

ExceptionOr<void> MediaSource::EndOfStream(optional<std::string> error) {
  if (ready_state != MediaSourceReadyState::OPEN) {
    return JsError::DOMException(
        InvalidStateError,
        R"(Cannot call endOfStream() unless MediaSource is "open".)");
  }
  for (auto& pair : source_buffers_) {
    if (pair.second->updating) {
      return JsError::DOMException(
          InvalidStateError,
          "Cannot call endOfStream() when a SourceBuffer is updating.");
    }
  }
  if (error.has_value()) {
    return JsError::DOMException(
        NotSupportedError,
        "Calling endOfStream() with an argument is not supported.");
  }

  ready_state = MediaSourceReadyState::ENDED;
  ScheduleEvent<events::Event>(EventType::SourceEnded);
  controller_.EndOfStream();
  return {};
}

double MediaSource::GetDuration() const {
  return controller_.GetPipelineManager()->GetDuration();
}

ExceptionOr<void> MediaSource::SetDuration(double duration) {
  if (std::isnan(duration))
    return JsError::TypeError("Cannot set duration to NaN.");
  if (ready_state != MediaSourceReadyState::OPEN) {
    return JsError::DOMException(
        InvalidStateError,
        R"(Cannot change duration unless MediaSource is "open".)");
  }
  for (auto& pair : source_buffers_) {
    if (pair.second->updating) {
      return JsError::DOMException(
          InvalidStateError,
          "Cannot change duration when a SourceBuffer is updating.");
    }
  }

  controller_.GetPipelineManager()->SetDuration(duration);
  return {};
}

void MediaSource::OpenMediaSource(RefPtr<HTMLVideoElement> video) {
  DCHECK(ready_state == MediaSourceReadyState::CLOSED)
      << "MediaSource already attached to a <video> element.";
  ready_state = MediaSourceReadyState::OPEN;
  video_element_ = video;
  ScheduleEvent<events::Event>(EventType::SourceOpen);
}

void MediaSource::CloseMediaSource() {
  DCHECK(ready_state != MediaSourceReadyState::CLOSED)
      << "MediaSource not attached to a <video> element.";

  ready_state = MediaSourceReadyState::CLOSED;
  video_element_.reset();
  controller_.Reset();

  for (auto& pair : source_buffers_) {
    pair.second->CloseMediaSource();
  }
  source_buffers_.clear();

  ScheduleEvent<events::Event>(EventType::SourceClose);
}

void MediaSource::OnReadyStateChanged(media::MediaReadyState ready_state) {
  if (video_element_)
    video_element_->OnReadyStateChanged(ready_state);
}

void MediaSource::OnPipelineStatusChanged(media::PipelineStatus status) {
  if (video_element_)
    video_element_->OnPipelineStatusChanged(status);
}

void MediaSource::OnMediaError(media::SourceType source, media::Status error) {
  if (video_element_)
    video_element_->OnMediaError(source, error);
}

void MediaSource::OnWaitingForKey() {
  if (video_element_)
    video_element_->ScheduleEvent<events::Event>(EventType::WaitingForKey);
}

void MediaSource::OnEncrypted(eme::MediaKeyInitDataType init_data_type,
                              ByteBuffer init_data) {
  if (video_element_) {
    video_element_->ScheduleEvent<events::MediaEncryptedEvent>(
        EventType::Encrypted, init_data_type, std::move(init_data));
  }
}


MediaSourceFactory::MediaSourceFactory() {
  AddListenerField(EventType::SourceOpen, &MediaSource::on_source_open);
  AddListenerField(EventType::SourceEnded, &MediaSource::on_source_ended);
  AddListenerField(EventType::SourceClose, &MediaSource::on_source_close);

  AddReadOnlyProperty("readyState", &MediaSource::ready_state);

  AddGenericProperty("duration", &MediaSource::GetDuration,
                     &MediaSource::SetDuration);

  AddMemberFunction("addSourceBuffer", &MediaSource::AddSourceBuffer);
  AddMemberFunction("endOfStream", &MediaSource::EndOfStream);

  AddStaticFunction("isTypeSupported", &MediaSource::IsTypeSupported);

  NotImplemented("activeSourceBuffers");
  NotImplemented("clearLiveSeekableRange");
  NotImplemented("removeSourceBuffer");
  NotImplemented("setLiveSeekableRange");
  NotImplemented("sourceBuffers");
}

}  // namespace mse
}  // namespace js
}  // namespace shaka

////////////////////////////////////////////////////////////

#include "..\..\core\ref_ptr.h"
#include "..\..\mapping\backing_object.h"

#include "time_ranges.h"

#include "..\events\media_key_message_event.h"
#include "..\eme\media_keys.h"
#include "..\eme\media_key_session.h"
#include "..\eme\media_key_system_access.h"

namespace shaka {

#define DEFINE_MEMORY_TRACEABLE_CASTS(Type) \
	template <> \
	memory::Traceable* TryCastToMemoryTraceable(Type* value) { \
		return static_cast<memory::Traceable*>(value); \
	}

//template <typename T>
//std::string GetBackingObjectName(const T*) {
//	return "";
//}

#define DEFINE_BACKINGOBJECT_NAME(Type) \
	template <> \
	std::string GetBackingObjectName(const Type* value) { \
		return value->name(); \
	} \
	template <> \
	std::string GetBackingObjectName(const RefPtr<Type>* value) { \
		return value->name(); \
	}

#define DEFINE_BACKINGOBJECT_CASTS(Type) \
	DEFINE_MEMORY_TRACEABLE_CASTS(Type) \
	DEFINE_BACKINGOBJECT_NAME(Type) \
	template <> \
	BackingObject* TryCastToBackingObject(Type* value) { \
		return static_cast<BackingObject*>(value); \
	} \
	template <> \
	Type* TryCastFromBackingObject(BackingObject* value) { \
		return static_cast<Type*>(value); \
	}

DEFINE_BACKINGOBJECT_NAME(js::events::MediaEncryptedEventInit)
DEFINE_BACKINGOBJECT_NAME(js::events::MediaKeyMessageEventInit)

DEFINE_BACKINGOBJECT_CASTS(js::events::MediaEncryptedEvent)
DEFINE_BACKINGOBJECT_CASTS(js::events::MediaKeyMessageEvent)

DEFINE_BACKINGOBJECT_NAME(js::eme::MediaKeySystemConfiguration)

DEFINE_BACKINGOBJECT_CASTS(js::mse::TimeRanges)
DEFINE_BACKINGOBJECT_CASTS(js::mse::MediaError)
DEFINE_BACKINGOBJECT_CASTS(js::mse::SourceBuffer)
DEFINE_BACKINGOBJECT_CASTS(js::mse::TextTrack)
DEFINE_BACKINGOBJECT_CASTS(js::mse::HTMLVideoElement)
DEFINE_BACKINGOBJECT_CASTS(js::mse::MediaSource)

DEFINE_BACKINGOBJECT_CASTS(js::eme::MediaKeys)
DEFINE_BACKINGOBJECT_CASTS(js::eme::MediaKeySession)
DEFINE_BACKINGOBJECT_CASTS(js::eme::MediaKeySystemAccess)

#undef DEFINE_MEMORY_TRACEABLE_CASTS
#undef DEFINE_BACKINGOBJECT_NAME
#undef DEFINE_BACKINGOBJECT_CASTS

}

