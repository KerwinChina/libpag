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

#include <unordered_map>

#include "Text.h"
#include "core/Canvas.h"
#include "pag/file.h"
#include "raster/PathEffect.h"

namespace pag {
std::shared_ptr<Graphic> Text::MakeFrom(const std::vector<GlyphHandle>& glyphs,
                                        const Rect* calculatedBounds) {
  if (glyphs.empty()) {
    return nullptr;
  }
  bool hasAlpha = false;
  Rect bounds = calculatedBounds ? *calculatedBounds : Rect::MakeEmpty();
  float maxStrokeWidth = 0;
  for (auto& glyph : glyphs) {
    auto glyphBounds = glyph->getBounds();
    glyph->getMatrix().mapRect(&glyphBounds);
    if (calculatedBounds == nullptr) {
      bounds.join(glyphBounds);
    }
    auto strokeWidth = glyph->getStrokeWidth();
    if (strokeWidth > maxStrokeWidth) {
      maxStrokeWidth = strokeWidth;
    }
    if (glyph->getAlpha() != Opaque) {
      hasAlpha = true;
    }
  }
  bounds.outset(maxStrokeWidth, maxStrokeWidth);
  return std::shared_ptr<Graphic>(new Text(glyphs, bounds, hasAlpha));
}

Text::Text(std::vector<GlyphHandle> glyphs, const Rect& bounds, bool hasAlpha)
    : glyphs(std::move(glyphs)), bounds(bounds), hasAlpha(hasAlpha) {
}

void Text::measureBounds(Rect* rect) const {
  *rect = bounds;
}

static Path GetStrokePath(const GlyphHandle& glyph, const Path& glyphPath) {
  if (glyph == nullptr || glyph->getStyle() == TextStyle::Fill || glyphPath.isEmpty()) {
    return {};
  }
  auto strokePath = glyphPath;
  Stroke stroke(glyph->getStrokeWidth());
  auto strokeEffect = PathEffect::MakeStroke(stroke);
  if (strokeEffect) {
    strokeEffect->applyTo(&strokePath);
  }
  return strokePath;
}

bool Text::hitTest(RenderCache*, float x, float y) {
  for (auto& glyph : glyphs) {
    auto local = Point::Make(x, y);
    Matrix invertMatrix = {};
    if (!glyph->getTotalMatrix().invert(&invertMatrix)) {
      continue;
    }
    invertMatrix.mapPoints(&local, 1);
    Path glyphPath = {};
    auto textFont = glyph->getFont();
    if (!textFont.getGlyphPath(glyph->getGlyphID(), &glyphPath)) {
      continue;
    }
    if (glyph->getStyle() == TextStyle::Fill || glyph->getStyle() == TextStyle::StrokeAndFill) {
      if (glyphPath.contains(local.x, local.y)) {
        return true;
      }
    }
    if (glyph->getStyle() == TextStyle::Stroke || glyph->getStyle() == TextStyle::StrokeAndFill) {
      auto strokePath = GetStrokePath(glyph, glyphPath);
      if (strokePath.contains(local.x, local.y)) {
        return true;
      }
    }
  }
  return false;
}

bool Text::getPath(Path* path) const {
  if (hasAlpha || path == nullptr) {
    return false;
  }
  Path textPath = {};
  for (auto& glyph : glyphs) {
    Path glyphPath = {};
    auto textFont = glyph->getFont();
    if (!textFont.getGlyphPath(glyph->getGlyphID(), &glyphPath)) {
      return false;
    }
    glyphPath.transform(glyph->getTotalMatrix());
    if (glyph->getStyle() == TextStyle::Fill || glyph->getStyle() == TextStyle::StrokeAndFill) {
      textPath.addPath(glyphPath);
    }
    auto strokePath = GetStrokePath(glyph, glyphPath);
    if (!strokePath.isEmpty()) {
      textPath.addPath(strokePath);
    }
  }
  path->addPath(textPath);
  return true;
}

void Text::prepare(RenderCache*) const {
}

static std::vector<PaintStyle> GetGlyphPaintStyles(const GlyphHandle& glyph) {
  std::vector<PaintStyle> styles = {};
  if (glyph->getStyle() == TextStyle::Fill) {
    styles.push_back(PaintStyle::Fill);
  } else if (glyph->getStyle() == TextStyle::Stroke) {
    styles.push_back(PaintStyle::Stroke);
  } else {
    if (glyph->getStrokeOverFill()) {
      styles.push_back(PaintStyle::Fill);
      styles.push_back(PaintStyle::Stroke);
    } else {
      styles.push_back(PaintStyle::Stroke);
      styles.push_back(PaintStyle::Fill);
    }
  }
  return styles;
}

void Text::draw(Canvas* canvas, RenderCache*) const {
  if (atlas == nullptr) {
    return;
  }
  atlas->draw(canvas);
  draw(canvas, false);
  draw(canvas, true);
}

void Text::draw(Canvas* canvas, bool colorGlyph) const {
  auto atlasTexture = colorGlyph ? atlas->getColorAtlasTexture() : atlas->getMaskAtlasTexture();
  std::vector<Matrix> matrices;
  std::vector<Rect> rects;
  std::vector<Color> colors;
  std::vector<Opacity> alphas;
  for (auto& glyph : glyphs) {
    if (!glyph->isVisible() || (colorGlyph != glyph->getFont().getTypeface()->hasColor())) {
      continue;
    }
    auto styles = GetGlyphPaintStyles(glyph);
    Rect location = Rect::MakeEmpty();
    for (auto style : styles) {
      if (!atlas->getLocation(glyph, style, &location)) {
        continue;
      }
      float strokeWidth = 0;
      Color color = glyph->getFillColor();
      if (style == PaintStyle::Stroke) {
        strokeWidth = glyph->getStrokeWidth();
        color = glyph->getStrokeColor();
      }
      auto glyphBounds = glyph->getBounds();
      Matrix invertedMatrix = Matrix::I();
      glyph->getExtraMatrix().invert(&invertedMatrix);
      auto originBounds = glyphBounds;
      invertedMatrix.mapRect(&originBounds);
      auto matrix = Matrix::I();
      matrix.postScale((originBounds.width() + strokeWidth * 2) / location.width(),
                       (originBounds.height() + strokeWidth * 2) / location.height());
      matrix.postTranslate(originBounds.x() - strokeWidth, originBounds.y() - strokeWidth);
      matrix.postConcat(glyph->getTotalMatrix());
      matrices.push_back(matrix);
      rects.push_back(location);
      colors.push_back(color);
      alphas.push_back(glyph->getAlpha());
    }
  }
  canvas->drawAtlas(atlasTexture.get(), &matrices[0], &rects[0], colorGlyph ? nullptr : &colors[0],
                    &alphas[0], matrices.size());
}
}  // namespace pag
