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

#include "TextAtlas.h"

#include "core/Canvas.h"
#include "gpu/Surface.h"
#include "rendering/graphics/Glyph.h"

namespace pag {
TextPaint CreateTextPaint_(const TextDocument* textDocument) {
  TextPaint textPaint = {};
  if (textDocument->applyFill && textDocument->applyStroke) {
    textPaint.style = TextStyle::StrokeAndFill;
  } else if (textDocument->applyStroke) {
    textPaint.style = TextStyle::Stroke;
  } else {
    textPaint.style = TextStyle::Fill;
  }
  textPaint.fillColor = textDocument->fillColor;
  textPaint.strokeColor = textDocument->strokeColor;
  textPaint.strokeWidth = textDocument->strokeWidth;
  textPaint.strokeOverFill = textDocument->strokeOverFill;
  textPaint.fontFamily = textDocument->fontFamily;
  textPaint.fontStyle = textDocument->fontStyle;
  textPaint.fontSize = textDocument->fontSize;
  textPaint.fauxBold = textDocument->fauxBold;
  textPaint.fauxItalic = textDocument->fauxItalic;
//  textPaint.isVertical = textDocument->direction == TextDirection::Vertical;
  return textPaint;
}

static void GetGlyphsFromTextDocument(const TextDocumentHandle& textDocument,
                                      std::vector<GlyphHandle>* glyphs) {
  auto textPaint = CreateTextPaint_(textDocument.get());
  auto tempGlyphs = Glyph::BuildFromText(textDocument->text, textPaint);
  glyphs->insert(glyphs->end(), tempGlyphs.begin(), tempGlyphs.end());
}

static std::vector<GlyphHandle> GetGlyphsFromSourceText(Property<TextDocumentHandle>* sourceText) {
  std::vector<GlyphHandle> glyphs;
  if (sourceText->animatable()) {
    auto keyframes =
        reinterpret_cast<AnimatableProperty<TextDocumentHandle>*>(sourceText)->keyframes;
    GetGlyphsFromTextDocument(keyframes[0]->startValue, &glyphs);
    for (const auto& keyframe : keyframes) {
      GetGlyphsFromTextDocument(keyframe->endValue, &glyphs);
    }
  } else {
    GetGlyphsFromTextDocument(sourceText->getValueAt(0), &glyphs);
  }
  return glyphs;
}

class RectanglePack {
 public:
  int width() const {
    return _width;
  }

  int height() const {
    return _height;
  }

  Point addRect(int w, int h) {
    w += Padding;
    h += Padding;
    auto area = (_width - x) * (_height - y);
    if ((x + w - _width) * y > area || (y + h - _height) * x > area) {
      if (_width <= _height) {
        x = _width;
        y = Padding;
        _width += w;
      } else {
        x = Padding;
        y = _height;
        _height += h;
      }
    }
    auto point = Point::Make(static_cast<float>(x), static_cast<float>(y));
    if (x + w - _width < y + h - _height) {
      x += w;
      _height = std::max(_height, y + h);
    } else {
      y += h;
      _width = std::max(_width, x + w);
    }
    return point;
  }

 private:
  static constexpr int Padding = 1;
  int _width = Padding;
  int _height = Padding;
  int x = Padding;
  int y = Padding;
};

static std::unique_ptr<Paint> CreateFillPaint(const Glyph* glyph) {
  if (glyph->getStyle() != TextStyle::Fill && glyph->getStyle() != TextStyle::StrokeAndFill) {
    return nullptr;
  }
  auto fillPaint = new Paint();
  fillPaint->setStyle(PaintStyle::Fill);
  return std::unique_ptr<Paint>(fillPaint);
}

static std::unique_ptr<Paint> CreateStrokePaint(const Glyph* glyph) {
  if (glyph->getStyle() != TextStyle::Stroke && glyph->getStyle() != TextStyle::StrokeAndFill) {
    return nullptr;
  }
  auto strokePaint = new Paint();
  strokePaint->setStyle(PaintStyle::Stroke);
  strokePaint->setStrokeWidth(glyph->getStrokeWidth());
  return std::unique_ptr<Paint>(strokePaint);
}

static std::unique_ptr<TextRun> MakeTextRun(
    const std::vector<Glyph*>& glyphs, RectanglePack* pack,
    std::unordered_map<BytesKey, Rect, BytesHasher>* glyphLocators) {
  if (glyphs.empty()) {
    return nullptr;
  }
  auto textRun = new TextRun();
  auto firstGlyph = glyphs[0];
  // Creates text paints.
  textRun->paints[0] = CreateFillPaint(firstGlyph).release();
  textRun->paints[1] = CreateStrokePaint(firstGlyph).release();
  auto textStyle = firstGlyph->getStyle();
  if ((textStyle == TextStyle::StrokeAndFill && !firstGlyph->getStrokeOverFill()) ||
      textRun->paints[0] == nullptr) {
    std::swap(textRun->paints[0], textRun->paints[1]);
  }
  // Creates text blob.
  auto noTranslateMatrix = firstGlyph->getTotalMatrix();
  noTranslateMatrix.setTranslateX(0);
  noTranslateMatrix.setTranslateY(0);
  textRun->matrix = noTranslateMatrix;
  noTranslateMatrix.invert(&noTranslateMatrix);
  std::vector<GlyphID> glyphIDs = {};
  for (auto& glyph : glyphs) {
    glyphIDs.push_back(glyph->getGlyphID());
    auto m = glyph->getTotalMatrix();
    m.postConcat(noTranslateMatrix);
    auto glyphWidth = static_cast<int>(glyph->getBounds().width());
    auto glyphHeight = static_cast<int>(glyph->getBounds().height());
    for (int i = 0; i < 2; ++i) {
      if (!textRun->paints[i]) {
        continue;
      }
      int strokeWidth = 0;
      if (textRun->paints[i]->getStyle() == PaintStyle::Stroke) {
        strokeWidth = static_cast<int>(ceil(glyph->getStrokeWidth()));
      }
      auto x = glyph->getBounds().x() - static_cast<float>(strokeWidth);
      auto y = glyph->getBounds().y() - static_cast<float>(strokeWidth);
      auto width = glyphWidth + strokeWidth * 2;
      auto height = glyphHeight + strokeWidth * 2;
      auto point = pack->addRect(width, height);
      textRun->positions[i].push_back(
          {m.getTranslateX() - x + point.x, m.getTranslateY() - y + point.y});
      BytesKey bytesKey;
      Atlas::ComputeAtlasKey(glyph, textRun->paints[i]->getStyle(), &bytesKey);
      (*glyphLocators)[bytesKey] =
          Rect::MakeXYWH(point.x, point.y, static_cast<float>(width), static_cast<float>(height));
    }
  }
  textRun->textFont = firstGlyph->getFont();
  textRun->glyphIDs = glyphIDs;
  return std::unique_ptr<TextRun>(textRun);
}

std::unique_ptr<Atlas> Atlas::Make(const std::vector<GlyphHandle>& glyphs, bool alphaOnly) {
  if (glyphs.empty()) {
    return nullptr;
  }
  std::unordered_map<BytesKey, std::vector<Glyph*>, BytesHasher> styleMap = {};
  for (auto& glyph : glyphs) {
    BytesKey styleKey = {};
    glyph->computeStyleKey(&styleKey);
    styleMap[styleKey].push_back(glyph.get());
  }
  RectanglePack pack;
  std::unordered_map<BytesKey, Rect, BytesHasher> glyphLocators;
  std::vector<TextRun*> textRuns;
  for (const auto& pair : styleMap) {
    auto textRun = MakeTextRun(pair.second, &pack, &glyphLocators).release();
    textRuns.push_back(textRun);
  }
  auto atlas = std::make_unique<Atlas>();
  atlas->alphaOnly = alphaOnly;
  atlas->width = pack.width();
  atlas->height = pack.height();
  atlas->textRuns = std::move(textRuns);
  atlas->glyphLocators = std::move(glyphLocators);
  return atlas;
}

std::unique_ptr<TextAtlas> TextAtlas::Make(Property<TextDocumentHandle>* sourceText,
                                           std::vector<TextAnimator*>*) {
  auto atlas = std::make_unique<TextAtlas>();
  auto glyphs = GetGlyphsFromSourceText(sourceText);
  if (glyphs.empty()) {
    return nullptr;
  }
  std::sort(glyphs.begin(), glyphs.end(), [](const GlyphHandle& a, const GlyphHandle& b) -> bool {
    auto aWidth = a->getBounds().width();
    auto aHeight = a->getBounds().height();
    auto bWidth = b->getBounds().width();
    auto bHeight = b->getBounds().height();
    return aWidth * aHeight > bWidth * bHeight || aWidth > bWidth || aHeight > bHeight;
  });
  std::vector<GlyphHandle> maskGlyphs = {};
  std::vector<GlyphHandle> colorGlyphs = {};
  for (auto& glyph : glyphs) {
    if (glyph->getFont().getTypeface()->hasColor()) {
      colorGlyphs.push_back(glyph);
    } else {
      maskGlyphs.push_back(glyph);
    }
  }
  atlas->maskAtlas = Atlas::Make(maskGlyphs).release();
  atlas->colorAtlas = Atlas::Make(colorGlyphs, false).release();
  return atlas;
}

void DrawTextRun(Canvas* canvas, const std::vector<TextRun*>& textRuns, int paintIndex) {
  auto totalMatrix = canvas->getMatrix();
  for (auto& textRun : textRuns) {
    auto textPaint = textRun->paints[paintIndex];
    if (!textPaint) {
      continue;
    }
    canvas->setMatrix(totalMatrix);
    canvas->concat(textRun->matrix);
    auto glyphs = &textRun->glyphIDs[0];
    auto positions = &textRun->positions[paintIndex][0];
    canvas->drawGlyphs(glyphs, positions, textRun->glyphIDs.size(), textRun->textFont, *textPaint);
  }
  canvas->setMatrix(totalMatrix);
}

void Atlas::draw(Canvas* canvas) {
  if (textRuns.empty()) {
    return;
  }
  auto surface = Surface::Make(canvas->getContext(), width, height, alphaOnly);
  auto atlasCanvas = surface->getCanvas();
  DrawTextRun(atlasCanvas, textRuns, 0);
  DrawTextRun(atlasCanvas, textRuns, 1);
  texture = surface->getTexture();
}

void Atlas::ComputeAtlasKey(const Glyph* glyph, PaintStyle style, BytesKey* atlasKey) {
  auto flags = static_cast<uint32_t>(glyph->getGlyphID());
  if (glyph->getFont().isFauxBold()) {
    flags |= 1 << 16;
  }
  if (glyph->getFont().isFauxItalic()) {
    flags |= 1 << 17;
  }
  flags |= (style == PaintStyle::Fill ? 1 : 0) << 18;
  atlasKey->write(flags);
  // TODO(pengweilv): typeface uniqueID
  //  atlasKey->write(textFont.getTypeface()->uniqueID());
  atlasKey->write(glyph->getFont().getSize());
  atlasKey->write(glyph->getStrokeWidth());
}

bool Atlas::getLocation(const GlyphHandle& glyph, PaintStyle style, Rect* location) {
  BytesKey bytesKey;
  ComputeAtlasKey(glyph.get(), style, &bytesKey);
  auto iter = glyphLocators.find(bytesKey);
  if (iter == glyphLocators.end()) {
    return false;
  }
  if (location) {
    *location = iter->second;
  }
  return true;
}

Atlas::~Atlas() {
  for (auto* textRun : textRuns) {
    delete textRun;
  }
}

TextAtlas::~TextAtlas() {
  delete maskAtlas;
  delete colorAtlas;
}

bool TextAtlas::getLocation(const GlyphHandle& glyph, PaintStyle style, Rect* location) {
  if (glyph->getFont().getTypeface()->hasColor()) {
    return colorAtlas && colorAtlas->getLocation(glyph, style, location);
  }
  return maskAtlas && maskAtlas->getLocation(glyph, style, location);
}

void TextAtlas::draw(Canvas* canvas) {
  if ((maskAtlas && maskAtlas->texture) || (colorAtlas && colorAtlas->texture)) {
    return;
  }
  if (maskAtlas) {
    maskAtlas->draw(canvas);
  }
  if (colorAtlas) {
    colorAtlas->draw(canvas);
  }
}
}  // namespace pag
