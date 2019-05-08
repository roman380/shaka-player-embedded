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

#include "src/js/dom/document.h"

#include "src/js/dom/comment.h"
#include "src/js/dom/element.h"
#include "src/js/dom/text.h"
#include "src/js/mse/video_element.h"
#include "src/memory/heap_tracer.h"
#include "src/util/clock.h"

namespace shaka {
namespace js {
namespace dom {

std::atomic<Document*> Document::instance_{nullptr};

Document::Document()
    : ContainerNode(DOCUMENT_NODE, nullptr),
      created_at_(util::Clock::Instance.GetMonotonicTime()) {}

// \cond Doxygen_Skip
Document::~Document() {
  if (instance_ == this)
    instance_ = nullptr;
}
// \endcond Doxygen_Skip

// static
Document* Document::CreateGlobalDocument() {
  DCHECK(instance_ == nullptr);
  return (instance_ = new Document());
}

std::string Document::node_name() const {
  return "#document";
}

optional<std::string> Document::NodeValue() const {
  return nullopt;
}

optional<std::string> Document::TextContent() const {
  return nullopt;
}

RefPtr<Element> Document::DocumentElement() const {
  for (auto& child : child_nodes()) {
    if (child->is_element())
      return static_cast<Element*>(child.get());
  }
  return nullptr;
}

RefPtr<Element> Document::CreateElement(const std::string& name) {
  if (name == "video") {
    // This should only be used in Shaka Player integration tests.
    return new mse::HTMLVideoElement(this);
  }
  return new Element(this, name, nullopt, nullopt);
}

RefPtr<Comment> Document::CreateComment(const std::string& data) {
  return new Comment(this, data);
}

RefPtr<Text> Document::CreateTextNode(const std::string& data) {
  return new Text(this, data);
}


DocumentFactory::DocumentFactory() {
  AddMemberFunction("createElement", &Document::CreateElement);
  AddMemberFunction("createComment", &Document::CreateComment);
  AddMemberFunction("createTextNode", &Document::CreateTextNode);

  AddGenericProperty("documentElement", &Document::DocumentElement);

  // TODO: Consider adding createEvent.  Shaka Player only uses it in the
  // Microsoft EME polyfill and the unit tests.
  NotImplemented("createEvent");

  NotImplemented("createElementNS");
  NotImplemented("createDocumentFragment");
  NotImplemented("createCDATASection");
  NotImplemented("createProcessingInstruction");

  NotImplemented("createAttribute");
  NotImplemented("createAttributeNS");
  NotImplemented("createRange");
  NotImplemented("createNodeIterator");
  NotImplemented("createTreeWalker");

  NotImplemented("importNode");
  NotImplemented("adoptNode");
}

}  // namespace dom
}  // namespace js
}  // namespace shaka

////////////////////////////////////////////////////////////

#include "..\..\core\ref_ptr.h"
#include "..\..\mapping\backing_object.h"

#include "attr.h"
#include "character_data.h"
#include "dom_exception.h"
#include "dom_parser.h"

#include "..\console.h"
#include "..\debug.h"
#include "..\location.h"
#include "..\navigator.h"
#include "..\test_type.h"
#include "..\xml_http_request.h"
#include "..\url.h"
#include "..\test_type.h"

#include "..\events\progress_event.h"

namespace shaka {

#define DEFINE_MEMORY_TRACEABLE_CASTS(Type) \
	template <> \
	memory::Traceable* TryCastToMemoryTraceable(Type* value) { \
		return static_cast<memory::Traceable*>(value); \
	}

DEFINE_MEMORY_TRACEABLE_CASTS(ByteBuffer)
DEFINE_MEMORY_TRACEABLE_CASTS(Promise)

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

DEFINE_BACKINGOBJECT_CASTS(BackingObject)

DEFINE_BACKINGOBJECT_NAME(Any)
DEFINE_BACKINGOBJECT_NAME(ByteBuffer)
DEFINE_BACKINGOBJECT_NAME(ByteString)
DEFINE_BACKINGOBJECT_NAME(Callback)

DEFINE_BACKINGOBJECT_NAME(js::TestTypeOptions)

DEFINE_BACKINGOBJECT_CASTS(js::Console)
DEFINE_BACKINGOBJECT_CASTS(js::Debug)
DEFINE_BACKINGOBJECT_CASTS(js::Location)
DEFINE_BACKINGOBJECT_CASTS(js::Navigator)
DEFINE_BACKINGOBJECT_CASTS(js::TestType)
DEFINE_BACKINGOBJECT_CASTS(js::XMLHttpRequest)
DEFINE_BACKINGOBJECT_CASTS(js::VTTCue)
DEFINE_BACKINGOBJECT_CASTS(js::URL)

DEFINE_BACKINGOBJECT_CASTS(js::events::EventTarget)
DEFINE_BACKINGOBJECT_CASTS(js::events::Event)
DEFINE_BACKINGOBJECT_CASTS(js::events::ProgressEvent)

DEFINE_BACKINGOBJECT_CASTS(js::dom::Node)
DEFINE_BACKINGOBJECT_CASTS(js::dom::Text)
DEFINE_BACKINGOBJECT_CASTS(js::dom::Comment)
DEFINE_BACKINGOBJECT_CASTS(js::dom::Element)
DEFINE_BACKINGOBJECT_CASTS(js::dom::DOMException)
DEFINE_BACKINGOBJECT_CASTS(js::dom::CharacterData)
DEFINE_BACKINGOBJECT_CASTS(js::dom::DOMParser)
DEFINE_BACKINGOBJECT_CASTS(js::dom::Attr)
DEFINE_BACKINGOBJECT_CASTS(js::dom::ContainerNode)
DEFINE_BACKINGOBJECT_CASTS(js::dom::Document)

#undef DEFINE_MEMORY_TRACEABLE_CASTS
#undef DEFINE_BACKINGOBJECT_NAME
#undef DEFINE_BACKINGOBJECT_CASTS

}
