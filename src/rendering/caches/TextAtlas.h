/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <unordered_map>

#include "core/Canvas.h"
#include "core/Paint.h"
#include "pag/file.h"
#include "pag/types.h"
#include "rendering/graphics/Glyph.h"

namespace pag {
class TextRun {
 public:
  ~TextRun() {
    delete paints[0];
    delete paints[1];
  }

  Matrix matrix = Matrix::I();
  Paint* paints[2] = {nullptr, nullptr};
  Font textFont = {};
  std::vector<GlyphID> glyphIDs;
  std::vector<Point> positions[2];
};

class Atlas {
 public:
  static std::unique_ptr<Atlas> Make(const std::vector<GlyphHandle>& glyphs, bool alphaOnly = true);

  ~Atlas();

  void draw(Canvas* canvas);

  static void ComputeAtlasKey(const Glyph* glyph, PaintStyle style, BytesKey* atlasKey);

  bool getLocation(const GlyphHandle& glyph, PaintStyle style, Rect* location);

 private:
  bool alphaOnly = true;
  int width = 0;
  int height = 0;
  std::vector<TextRun*> textRuns;
  std::shared_ptr<Texture> texture;
  std::unordered_map<BytesKey, Rect, BytesHasher> glyphLocators;

  friend class TextAtlas;
};

class TextAtlas {
 public:
  static std::unique_ptr<TextAtlas> Make(Property<TextDocumentHandle>* sourceText,
                                         std::vector<TextAnimator*>* animators);

  ~TextAtlas();

  bool getLocation(const GlyphHandle& glyph, PaintStyle style, Rect* location);

  std::shared_ptr<Texture> getMaskAtlasTexture() const {
    return maskAtlas ? maskAtlas->texture : nullptr;
  }

  std::shared_ptr<Texture> getColorAtlasTexture() const {
    return colorAtlas ? colorAtlas->texture : nullptr;
  }

  void draw(Canvas* canvas);

 private:
  Atlas* maskAtlas = nullptr;
  Atlas* colorAtlas = nullptr;
};
}  // namespace pag
