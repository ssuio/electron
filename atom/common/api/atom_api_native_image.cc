// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/common/api/atom_api_native_image.h"

#include <string>
#include <vector>

#include "atom/common/native_mate_converters/file_path_converter.h"
#include "atom/common/native_mate_converters/gfx_converter.h"
#include "base/files/file_util.h"
#include "base/strings/string_util.h"
#include "native_mate/dictionary.h"
#include "native_mate/object_template_builder.h"
#include "ui/base/layout.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_util.h"

#include "atom/common/node_includes.h"

namespace atom {

namespace api {

namespace {

#if !defined(OS_MACOSX)
struct ScaleFactorPair {
  const char* name;
  float scale;
};

ScaleFactorPair kScaleFactorPairs[] = {
  // The "@2x" is put as first one to make scale matching faster.
  { "@2x"    , 2.0f },
  { "@3x"    , 3.0f },
  { "@1x"    , 1.0f },
  { "@4x"    , 4.0f },
  { "@5x"    , 5.0f },
  { "@1.25x" , 1.25f },
  { "@1.33x" , 1.33f },
  { "@1.4x"  , 1.4f },
  { "@1.5x"  , 1.5f },
  { "@1.8x"  , 1.8f },
  { "@2.5x"  , 2.5f },
};

float GetScaleFactorFromPath(const base::FilePath& path) {
  std::string filename(path.BaseName().RemoveExtension().AsUTF8Unsafe());

  // We don't try to convert string to float here because it is very very
  // expensive.
  for (unsigned i = 0; i < arraysize(kScaleFactorPairs); ++i) {
    if (EndsWith(filename, kScaleFactorPairs[i].name, true))
      return kScaleFactorPairs[i].scale;
  }

  return 1.0f;
}

bool AddImageSkiaRep(gfx::ImageSkia* image,
                     const base::FilePath& path,
                     double scale_factor) {
  std::string file_contents;
  if (!base::ReadFileToString(path, &file_contents))
    return false;

  const unsigned char* data =
      reinterpret_cast<const unsigned char*>(file_contents.data());
  size_t size = file_contents.size();
  scoped_ptr<SkBitmap> decoded(new SkBitmap());

  // Try PNG first.
  if (!gfx::PNGCodec::Decode(data, size, decoded.get()))
    // Try JPEG.
    decoded.reset(gfx::JPEGCodec::Decode(data, size));

  if (!decoded)
    return false;

  image->AddRepresentation(gfx::ImageSkiaRep(*decoded.release(), scale_factor));
  return true;
}

bool PopulateImageSkiaRepsFromPath(gfx::ImageSkia* image,
                                   const base::FilePath& path) {
  bool succeed = false;
  std::string filename(path.BaseName().RemoveExtension().AsUTF8Unsafe());
  if (MatchPattern(filename, "*@*x"))
    // Don't search for other representations if the DPI has been specified.
    return AddImageSkiaRep(image, path, GetScaleFactorFromPath(path));
  else
    succeed |= AddImageSkiaRep(image, path, 1.0f);

  for (const ScaleFactorPair& pair : kScaleFactorPairs)
    succeed |= AddImageSkiaRep(image,
                               path.InsertBeforeExtensionASCII(pair.name),
                               pair.scale);
  return succeed;
}
#endif  // !defined(OS_MACOSX)

v8::Persistent<v8::ObjectTemplate> template_;

}  // namespace

NativeImage::NativeImage() {}

NativeImage::NativeImage(const gfx::Image& image) : image_(image) {}

NativeImage::~NativeImage() {}

mate::ObjectTemplateBuilder NativeImage::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  if (template_.IsEmpty())
    template_.Reset(isolate, mate::ObjectTemplateBuilder(isolate)
        .SetMethod("toPng", &NativeImage::ToPNG)
        .SetMethod("toJpeg", &NativeImage::ToJPEG)
        .SetMethod("isEmpty", &NativeImage::IsEmpty)
        .SetMethod("getSize", &NativeImage::GetSize)
        .Build());

  return mate::ObjectTemplateBuilder(
      isolate, v8::Local<v8::ObjectTemplate>::New(isolate, template_));
}

v8::Handle<v8::Value> NativeImage::ToPNG(v8::Isolate* isolate) {
  scoped_refptr<base::RefCountedMemory> png = image_.As1xPNGBytes();
  return node::Buffer::New(isolate,
                           reinterpret_cast<const char*>(png->front()),
                           png->size());
}

v8::Handle<v8::Value> NativeImage::ToJPEG(v8::Isolate* isolate, int quality) {
  std::vector<unsigned char> output;
  gfx::JPEG1xEncodedDataFromImage(image_, quality, &output);
  return node::Buffer::New(isolate,
                           reinterpret_cast<const char*>(&output.front()),
                           output.size());
}

bool NativeImage::IsEmpty() {
  return image_.IsEmpty();
}

gfx::Size NativeImage::GetSize() {
  return image_.Size();
}

// static
mate::Handle<NativeImage> NativeImage::Create(
    v8::Isolate* isolate, const gfx::Image& image) {
  return mate::CreateHandle(isolate, new NativeImage(image));
}

// static
mate::Handle<NativeImage> NativeImage::CreateFromPNG(
    v8::Isolate* isolate, v8::Handle<v8::Value> buffer) {
  gfx::Image image = gfx::Image::CreateFrom1xPNGBytes(
      reinterpret_cast<unsigned char*>(node::Buffer::Data(buffer)),
      node::Buffer::Length(buffer));
  return Create(isolate, image);
}

// static
mate::Handle<NativeImage> NativeImage::CreateFromJPEG(
    v8::Isolate* isolate, v8::Handle<v8::Value> buffer) {
  gfx::Image image = gfx::ImageFrom1xJPEGEncodedData(
      reinterpret_cast<unsigned char*>(node::Buffer::Data(buffer)),
      node::Buffer::Length(buffer));
  return Create(isolate, image);
}

#if !defined(OS_MACOSX)
// static
mate::Handle<NativeImage> NativeImage::CreateFromPath(
    v8::Isolate* isolate, const base::FilePath& path) {
  gfx::ImageSkia image_skia;
  PopulateImageSkiaRepsFromPath(&image_skia, path);
  return Create(isolate, gfx::Image(image_skia));
}
#endif  // !defined(OS_MACOSX)

}  // namespace api

}  // namespace atom


namespace {

void Initialize(v8::Handle<v8::Object> exports, v8::Handle<v8::Value> unused,
                v8::Handle<v8::Context> context, void* priv) {
  mate::Dictionary dict(context->GetIsolate(), exports);
  dict.SetMethod("createFromPng", &atom::api::NativeImage::CreateFromPNG);
  dict.SetMethod("createFromJpeg", &atom::api::NativeImage::CreateFromJPEG);
  dict.SetMethod("createFromPath", &atom::api::NativeImage::CreateFromPath);
}

}  // namespace

NODE_MODULE_CONTEXT_AWARE_BUILTIN(atom_common_native_image, Initialize)
