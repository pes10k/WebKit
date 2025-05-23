use ffi::{FillLinearParams, FillRadialParams};
// Copyright 2023 Google LLC
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.
use font_types::{BoundingBox, GlyphId};
use read_fonts::{
    tables::{colr::CompositeMode, cpal::Cpal, os2::SelectionFlags},
    FileRef, FontRef, ReadError, TableProvider,
};
use skrifa::{
    attribute::Style,
    charmap::MappingIndex,
    color::{Brush, ColorGlyphFormat, ColorPainter, Transform},
    instance::{Location, Size},
    metrics::{GlyphMetrics, Metrics},
    outline::{
        pen::NullPen, DrawSettings, Engine, HintingInstance, HintingOptions, OutlineGlyphFormat,
        OutlinePen, SmoothMode, Target,
    },
    setting::VariationSetting,
    string::{LocalizedStrings, StringId},
    MetadataProvider, OutlineGlyphCollection, Tag,
};
use std::pin::Pin;

use crate::bitmap::{bitmap_glyph, bitmap_metrics, has_bitmap_glyph, png_data, BridgeBitmapGlyph};

use crate::ffi::{
    AutoHintingControl, AxisWrapper, BridgeFontStyle, BridgeLocalizedName, BridgeScalerMetrics,
    ClipBox, ColorPainterWrapper, ColorStop, FfiPoint, PaletteOverride, SkiaDesignCoordinate,
};

const PATH_EXTRACTION_RESERVE: usize = 150;

fn make_mapping_index<'a>(font_ref: &'a BridgeFontRef) -> Box<BridgeMappingIndex> {
    font_ref
        .with_font(|f| Some(Box::new(BridgeMappingIndex(MappingIndex::new(f)))))
        .unwrap()
}

unsafe fn hinting_reliant<'a>(font_ref: &'a BridgeOutlineCollection) -> bool {
    if let Some(outlines) = &font_ref.0 {
        outlines.require_interpreter()
    } else {
        false
    }
}

unsafe fn no_hinting_instance<'a>() -> Box<BridgeHintingInstance> {
    Box::new(BridgeHintingInstance(None))
}

unsafe fn make_hinting_instance<'a>(
    outlines: &BridgeOutlineCollection,
    size: f32,
    coords: &BridgeNormalizedCoords,
    do_light_hinting: bool,
    do_lcd_antialiasing: bool,
    lcd_orientation_vertical: bool,
    autohinting_control: AutoHintingControl,
) -> Box<BridgeHintingInstance> {
    let hinting_instance = match &outlines.0 {
        Some(outlines) => {
            let smooth_mode = match (
                do_light_hinting,
                do_lcd_antialiasing,
                lcd_orientation_vertical,
            ) {
                (true, _, _) => SmoothMode::Light,
                (false, true, false) => SmoothMode::Lcd,
                (false, true, true) => SmoothMode::VerticalLcd,
                _ => SmoothMode::Normal,
            };

            let hinting_target = Target::Smooth {
                mode: smooth_mode,
                // See https://docs.rs/skrifa/latest/skrifa/outline/enum.Target.html#variant.Smooth.field.mode
                // Configure additional params to match FreeType.
                symmetric_rendering: true,
                preserve_linear_metrics: false,
            };

            // Do not force-autohint for CFF to match FreeType, compare
            // https://gitlab.freedesktop.org/freetype/freetype/-/blob/57617782464411201ce7bbc93b086c1b4d7d84a5/src/base/ftobjs.c#L1001
            // Engine::AutoFallback (see Skrifa docs) means:
            // "Specifically, PostScript (CFF/CFF2) fonts will always use the hinting engine in the
            // PostScript interpreter and TrueType fonts will use the interpreter for TrueType
            // instructions if one of the fpgm or prep tables is non-empty, falling back to the
            // automatic hinter otherwise."
            // So Engine::AutoFallback does not engage autohinting for CFF.
            let engine_type = match (autohinting_control, outlines.format()) {
                (AutoHintingControl::ForceForGlyfAndCff, _) => Engine::Auto(None),
                (
                    AutoHintingControl::PreferAutoOverHintsForGlyf,
                    Some(OutlineGlyphFormat::Glyf),
                ) => Engine::Auto(None),
                _ => Engine::AutoFallback,
            };

            HintingInstance::new(
                outlines,
                Size::new(size),
                &coords.normalized_coords,
                HintingOptions {
                    engine: engine_type,
                    target: hinting_target,
                },
            )
            .ok()
        }
        _ => None,
    };
    Box::new(BridgeHintingInstance(hinting_instance))
}

unsafe fn make_mono_hinting_instance<'a>(
    outlines: &BridgeOutlineCollection,
    size: f32,
    coords: &BridgeNormalizedCoords,
) -> Box<BridgeHintingInstance> {
    let hinting_instance = outlines.0.as_ref().and_then(|outlines| {
        HintingInstance::new(
            outlines,
            Size::new(size),
            &coords.normalized_coords,
            skrifa::outline::HintingMode::Strong,
        )
        .ok()
    });
    Box::new(BridgeHintingInstance(hinting_instance))
}

fn lookup_glyph_or_zero(
    font_ref: &BridgeFontRef,
    map: &BridgeMappingIndex,
    codepoints: &[u32],
    glyphs: &mut [u16],
) {
    glyphs.fill(0);
    font_ref.with_font(|f| {
        let mappings = map.0.charmap(f);
        for it in codepoints.iter().zip(glyphs.iter_mut()) {
            let (codepoint, glyph) = it;
            // Remove u16 conversion when implementing large glyph id support in Skia.
            *glyph = u16::try_from(mappings.map(*codepoint).unwrap_or_default().to_u32())
                .unwrap_or_default();
        }
        Some(())
    });
}

fn num_glyphs(font_ref: &BridgeFontRef) -> u16 {
    font_ref
        .with_font(|f| Some(f.maxp().ok()?.num_glyphs()))
        .unwrap_or_default()
}

fn fill_glyph_to_unicode_map(font_ref: &BridgeFontRef, map: &mut [u32]) {
    map.fill(0);
    font_ref.with_font(|f| {
        let mappings = f.charmap().mappings();
        for item in mappings {
            if map[item.1.to_u32() as usize] == 0 {
                map[item.1.to_u32() as usize] = item.0;
            }
        }
        Some(())
    });
}

struct VerbsPointsPen<'a> {
    verbs: &'a mut Vec<u8>,
    points: &'a mut Vec<FfiPoint>,
    started: bool,
    current: FfiPoint,
}

impl FfiPoint {
    fn new(x: f32, y: f32) -> Self {
        Self { x, y }
    }
}

// Values need to match SkPathVerb.
#[repr(u8)]
enum PathVerb {
    MoveTo = 0,
    LineTo = 1,
    QuadTo = 2,
    CubicTo = 4,
    Close = 5,
}

impl<'a> VerbsPointsPen<'a> {
    fn new(verbs: &'a mut Vec<u8>, points: &'a mut Vec<FfiPoint>) -> Self {
        verbs.clear();
        points.clear();
        verbs.reserve(PATH_EXTRACTION_RESERVE);
        points.reserve(PATH_EXTRACTION_RESERVE);
        Self {
            verbs,
            points,
            started: false,
            current: FfiPoint::default(),
        }
    }

    fn going_to(&mut self, point: &FfiPoint) {
        if !self.started {
            self.started = true;
            self.verbs.push(PathVerb::MoveTo as u8);
            self.points.push(self.current);
        }
        self.current = *point;
    }

    fn current_is_not(&self, point: &FfiPoint) -> bool {
        self.current != *point
    }
}

impl<'a> OutlinePen for VerbsPointsPen<'a> {
    fn move_to(&mut self, x: f32, y: f32) {
        let pt0 = FfiPoint::new(x, -y);
        if self.started {
            self.close();
            self.started = false;
        }
        self.current = pt0;
    }

    fn line_to(&mut self, x: f32, y: f32) {
        let pt0 = FfiPoint::new(x, -y);
        if self.current_is_not(&pt0) {
            self.going_to(&pt0);
            self.verbs.push(PathVerb::LineTo as u8);
            self.points.push(pt0);
        }
    }

    fn quad_to(&mut self, cx0: f32, cy0: f32, x: f32, y: f32) {
        let pt0 = FfiPoint::new(cx0, -cy0);
        let pt1 = FfiPoint::new(x, -y);
        if self.current_is_not(&pt0) || self.current_is_not(&pt1) {
            self.going_to(&pt1);
            self.verbs.push(PathVerb::QuadTo as u8);
            self.points.push(pt0);
            self.points.push(pt1);
        }
    }

    fn curve_to(&mut self, cx0: f32, cy0: f32, cx1: f32, cy1: f32, x: f32, y: f32) {
        let pt0 = FfiPoint::new(cx0, -cy0);
        let pt1 = FfiPoint::new(cx1, -cy1);
        let pt2 = FfiPoint::new(x, -y);
        if self.current_is_not(&pt0) || self.current_is_not(&pt1) || self.current_is_not(&pt2) {
            self.going_to(&pt2);
            self.verbs.push(PathVerb::CubicTo as u8);
            self.points.push(pt0);
            self.points.push(pt1);
            self.points.push(pt2);
        }
    }

    fn close(&mut self) {
        if let Some(verb) = self.verbs.last().cloned() {
            if verb == PathVerb::QuadTo as u8
                || verb == PathVerb::CubicTo as u8
                || verb == PathVerb::LineTo as u8
                || verb == PathVerb::MoveTo as u8
            {
                self.verbs.push(PathVerb::Close as u8);
            }
        }
    }
}

struct ColorPainterImpl<'a> {
    color_painter_wrapper: Pin<&'a mut ffi::ColorPainterWrapper>,
    clip_level: usize,
}

impl<'a> ColorPainter for ColorPainterImpl<'a> {
    fn push_transform(&mut self, transform: Transform) {
        if self.clip_level > 0 {
            return;
        }
        self.color_painter_wrapper
            .as_mut()
            .push_transform(&ffi::Transform {
                xx: transform.xx,
                xy: transform.xy,
                yx: transform.yx,
                yy: transform.yy,
                dx: transform.dx,
                dy: transform.dy,
            });
    }

    fn pop_transform(&mut self) {
        if self.clip_level > 0 {
            return;
        }
        self.color_painter_wrapper.as_mut().pop_transform();
    }

    fn push_clip_glyph(&mut self, glyph: GlyphId) {
        if self.clip_level == 0 {
            // TODO(drott): Handle large glyph ids in clip operation.
            self.color_painter_wrapper
                .as_mut()
                .push_clip_glyph(glyph.to_u32().try_into().ok().unwrap_or_default());
        }
        if self.color_painter_wrapper.as_mut().is_bounds_mode() {
            self.clip_level += 1;
        }
    }

    fn push_clip_box(&mut self, clip_box: BoundingBox<f32>) {
        if self.clip_level == 0 {
            self.color_painter_wrapper.as_mut().push_clip_rectangle(
                clip_box.x_min,
                clip_box.y_min,
                clip_box.x_max,
                clip_box.y_max,
            );
        }
        if self.color_painter_wrapper.as_mut().is_bounds_mode() {
            self.clip_level += 1;
        }
    }

    fn pop_clip(&mut self) {
        if self.color_painter_wrapper.as_mut().is_bounds_mode() {
            self.clip_level -= 1;
        }
        if self.clip_level == 0 {
            self.color_painter_wrapper.as_mut().pop_clip();
        }
    }

    fn fill(&mut self, fill_type: Brush) {
        if self.clip_level > 0 {
            return;
        }
        let color_painter = self.color_painter_wrapper.as_mut();
        match fill_type {
            Brush::Solid {
                palette_index,
                alpha,
            } => {
                color_painter.fill_solid(palette_index, alpha);
            }

            Brush::LinearGradient {
                p0,
                p1,
                color_stops,
                extend,
            } => {
                let mut bridge_color_stops = BridgeColorStops {
                    stops_iterator: Box::new(color_stops.iter()),
                    num_stops: color_stops.len(),
                };
                color_painter.fill_linear(
                    &FillLinearParams {
                        x0: p0.x,
                        y0: p0.y,
                        x1: p1.x,
                        y1: p1.y,
                    },
                    &mut bridge_color_stops,
                    extend as u8,
                );
            }
            Brush::RadialGradient {
                c0,
                r0,
                c1,
                r1,
                color_stops,
                extend,
            } => {
                let mut bridge_color_stops = BridgeColorStops {
                    stops_iterator: Box::new(color_stops.iter()),
                    num_stops: color_stops.len(),
                };
                color_painter.fill_radial(
                    &FillRadialParams {
                        x0: c0.x,
                        y0: c0.y,
                        r0,
                        x1: c1.x,
                        y1: c1.y,
                        r1,
                    },
                    &mut bridge_color_stops,
                    extend as u8,
                );
            }
            Brush::SweepGradient {
                c0,
                start_angle,
                end_angle,
                color_stops,
                extend,
            } => {
                let mut bridge_color_stops = BridgeColorStops {
                    stops_iterator: Box::new(color_stops.iter()),
                    num_stops: color_stops.len(),
                };
                color_painter.fill_sweep(
                    &ffi::FillSweepParams {
                        x0: c0.x,
                        y0: c0.y,
                        start_angle,
                        end_angle,
                    },
                    &mut bridge_color_stops,
                    extend as u8,
                );
            }
        }
    }

    fn fill_glyph(&mut self, glyph: GlyphId, brush_transform: Option<Transform>, brush: Brush) {
        if self.color_painter_wrapper.as_mut().is_bounds_mode() {
            self.push_clip_glyph(glyph);
            self.pop_clip();
            return;
        }

        let color_painter = self.color_painter_wrapper.as_mut();
        let brush_transform = brush_transform.unwrap_or_default();
        match brush {
            Brush::Solid {
                palette_index,
                alpha,
            } => {
                // TODO(drott): Handle large glyph ids in fill glyph operation.
                color_painter.fill_glyph_solid(
                    glyph.to_u32().try_into().ok().unwrap_or_default(),
                    palette_index,
                    alpha,
                );
            }
            Brush::LinearGradient {
                p0,
                p1,
                color_stops,
                extend,
            } => {
                let mut bridge_color_stops = BridgeColorStops {
                    stops_iterator: Box::new(color_stops.iter()),
                    num_stops: color_stops.len(),
                };
                color_painter.fill_glyph_linear(
                    // TODO(drott): Handle large glyph ids in fill glyph operation.
                    glyph.to_u32().try_into().ok().unwrap_or_default(),
                    &ffi::Transform {
                        xx: brush_transform.xx,
                        xy: brush_transform.xy,
                        yx: brush_transform.yx,
                        yy: brush_transform.yy,
                        dx: brush_transform.dx,
                        dy: brush_transform.dy,
                    },
                    &FillLinearParams {
                        x0: p0.x,
                        y0: p0.y,
                        x1: p1.x,
                        y1: p1.y,
                    },
                    &mut bridge_color_stops,
                    extend as u8,
                );
            }
            Brush::RadialGradient {
                c0,
                r0,
                c1,
                r1,
                color_stops,
                extend,
            } => {
                let mut bridge_color_stops = BridgeColorStops {
                    stops_iterator: Box::new(color_stops.iter()),
                    num_stops: color_stops.len(),
                };
                color_painter.fill_glyph_radial(
                    // TODO(drott): Handle large glyph ids in fill glyph operation.
                    glyph.to_u32().try_into().ok().unwrap_or_default(),
                    &ffi::Transform {
                        xx: brush_transform.xx,
                        xy: brush_transform.xy,
                        yx: brush_transform.yx,
                        yy: brush_transform.yy,
                        dx: brush_transform.dx,
                        dy: brush_transform.dy,
                    },
                    &FillRadialParams {
                        x0: c0.x,
                        y0: c0.y,
                        r0,
                        x1: c1.x,
                        y1: c1.y,
                        r1,
                    },
                    &mut bridge_color_stops,
                    extend as u8,
                );
            }
            Brush::SweepGradient {
                c0,
                start_angle,
                end_angle,
                color_stops,
                extend,
            } => {
                let mut bridge_color_stops = BridgeColorStops {
                    stops_iterator: Box::new(color_stops.iter()),
                    num_stops: color_stops.len(),
                };
                color_painter.fill_glyph_sweep(
                    // TODO(drott): Handle large glyph ids in fill glyph operation.
                    glyph.to_u32().try_into().ok().unwrap_or_default(),
                    &ffi::Transform {
                        xx: brush_transform.xx,
                        xy: brush_transform.xy,
                        yx: brush_transform.yx,
                        yy: brush_transform.yy,
                        dx: brush_transform.dx,
                        dy: brush_transform.dy,
                    },
                    &ffi::FillSweepParams {
                        x0: c0.x,
                        y0: c0.y,
                        start_angle,
                        end_angle,
                    },
                    &mut bridge_color_stops,
                    extend as u8,
                );
            }
        }
    }

    fn push_layer(&mut self, composite_mode: CompositeMode) {
        if self.clip_level > 0 {
            return;
        }
        self.color_painter_wrapper
            .as_mut()
            .push_layer(composite_mode as u8);
    }
    fn pop_layer(&mut self) {
        if self.clip_level > 0 {
            return;
        }
        self.color_painter_wrapper.as_mut().pop_layer();
    }
}

fn get_path_verbs_points(
    outlines: &BridgeOutlineCollection,
    glyph_id: u16,
    size: f32,
    coords: &BridgeNormalizedCoords,
    hinting_instance: &BridgeHintingInstance,
    verbs: &mut Vec<u8>,
    points: &mut Vec<FfiPoint>,
    scaler_metrics: &mut BridgeScalerMetrics,
) -> bool {
    outlines
        .0
        .as_ref()
        .and_then(|outlines| {
            let glyph = outlines.get(GlyphId::from(glyph_id))?;

            let draw_settings = match &hinting_instance.0 {
                Some(instance) => DrawSettings::hinted(instance, false),
                _ => DrawSettings::unhinted(Size::new(size), &coords.normalized_coords),
            };

            let mut verbs_points_pen = VerbsPointsPen::new(verbs, points);
            match glyph.draw(draw_settings, &mut verbs_points_pen) {
                Err(_) => None,
                Ok(metrics) => {
                    scaler_metrics.has_overlaps = metrics.has_overlaps;
                    Some(())
                }
            }
        })
        .is_some()
}

fn shrink_verbs_points_if_needed(verbs: &mut Vec<u8>, points: &mut Vec<FfiPoint>) {
    verbs.shrink_to(PATH_EXTRACTION_RESERVE);
    points.shrink_to(PATH_EXTRACTION_RESERVE);
}

fn unhinted_advance_width_or_zero(
    font_ref: &BridgeFontRef,
    size: f32,
    coords: &BridgeNormalizedCoords,
    glyph_id: u16,
) -> f32 {
    font_ref
        .with_font(|f| {
            GlyphMetrics::new(f, Size::new(size), coords.normalized_coords.coords())
                .advance_width(GlyphId::from(glyph_id))
        })
        .unwrap_or_default()
}

fn scaler_hinted_advance_width(
    outlines: &BridgeOutlineCollection,
    hinting_instance: &BridgeHintingInstance,
    glyph_id: u16,
    out_advance_width: &mut f32,
) -> bool {
    hinting_instance
        .0
        .as_ref()
        .and_then(|instance| {
            let draw_settings = DrawSettings::hinted(instance, false);

            let outlines = outlines.0.as_ref()?;
            let glyph = outlines.get(GlyphId::from(glyph_id))?;
            let mut null_pen = NullPen {};
            let adjusted_metrics = glyph.draw(draw_settings, &mut null_pen).ok()?;
            adjusted_metrics.advance_width.map(|adjusted_advance| {
                *out_advance_width = adjusted_advance;
                ()
            })
        })
        .is_some()
}

fn units_per_em_or_zero(font_ref: &BridgeFontRef) -> u16 {
    font_ref
        .with_font(|f| Some(f.head().ok()?.units_per_em()))
        .unwrap_or_default()
}

fn convert_metrics(skrifa_metrics: &Metrics) -> ffi::Metrics {
    ffi::Metrics {
        top: skrifa_metrics.bounds.map_or(0.0, |b| b.y_max),
        bottom: skrifa_metrics.bounds.map_or(0.0, |b| b.y_min),
        x_min: skrifa_metrics.bounds.map_or(0.0, |b| b.x_min),
        x_max: skrifa_metrics.bounds.map_or(0.0, |b| b.x_max),
        ascent: skrifa_metrics.ascent,
        descent: skrifa_metrics.descent,
        leading: skrifa_metrics.leading,
        avg_char_width: skrifa_metrics.average_width.unwrap_or(0.0),
        max_char_width: skrifa_metrics.max_width.unwrap_or(0.0),
        x_height: -skrifa_metrics.x_height.unwrap_or(0.0),
        cap_height: -skrifa_metrics.cap_height.unwrap_or(0.0),
        underline_position: skrifa_metrics.underline.map_or(f32::NAN, |u| u.offset),
        underline_thickness: skrifa_metrics.underline.map_or(f32::NAN, |u| u.thickness),
        strikeout_position: skrifa_metrics.strikeout.map_or(f32::NAN, |s| s.offset),
        strikeout_thickness: skrifa_metrics.strikeout.map_or(f32::NAN, |s| s.thickness),
    }
}

fn get_skia_metrics(
    font_ref: &BridgeFontRef,
    size: f32,
    coords: &BridgeNormalizedCoords,
) -> ffi::Metrics {
    font_ref
        .with_font(|f| {
            let fontations_metrics =
                Metrics::new(f, Size::new(size), coords.normalized_coords.coords());
            Some(convert_metrics(&fontations_metrics))
        })
        .unwrap_or_default()
}

fn get_unscaled_metrics(font_ref: &BridgeFontRef, coords: &BridgeNormalizedCoords) -> ffi::Metrics {
    font_ref
        .with_font(|f| {
            let fontations_metrics =
                Metrics::new(f, Size::unscaled(), coords.normalized_coords.coords());
            Some(convert_metrics(&fontations_metrics))
        })
        .unwrap_or_default()
}

fn get_localized_strings<'a>(font_ref: &'a BridgeFontRef<'a>) -> Box<BridgeLocalizedStrings<'a>> {
    Box::new(BridgeLocalizedStrings {
        localized_strings: font_ref
            .with_font(|f| Some(f.localized_strings(StringId::FAMILY_NAME)))
            .unwrap_or_default(),
    })
}

fn localized_name_next(
    bridge_localized_strings: &mut BridgeLocalizedStrings,
    out_localized_name: &mut BridgeLocalizedName,
) -> bool {
    match bridge_localized_strings.localized_strings.next() {
        Some(localized_string) => {
            out_localized_name.string = localized_string.to_string();
            // TODO(b/307906051): Remove the suffix before shipping.
            out_localized_name.string.push_str(" (Fontations)");
            out_localized_name.language = localized_string
                .language()
                .map(|l| l.to_string())
                .unwrap_or_default();
            true
        }
        _ => false,
    }
}

fn english_or_first_font_name(font_ref: &BridgeFontRef, name_id: StringId) -> Option<String> {
    font_ref.with_font(|f| {
        f.localized_strings(name_id)
            .english_or_first()
            .map(|localized_string| localized_string.to_string())
    })
}

fn family_name(font_ref: &BridgeFontRef) -> String {
    font_ref
        .with_font(|f| {
            // https://learn.microsoft.com/en-us/typography/opentype/spec/os2#fsselection
            // Bit 8 of the `fsSelection' field in the `OS/2' table indicates a WWS-only font face.
            // When this bit is set it means *do not* use the WWS strings.
            let use_wws = !f
                .os2()
                .map(|t| t.fs_selection().contains(SelectionFlags::WWS))
                .unwrap_or_default();
            use_wws
                .then(|| english_or_first_font_name(font_ref, StringId::WWS_FAMILY_NAME))
                .flatten()
                .or_else(|| english_or_first_font_name(font_ref, StringId::TYPOGRAPHIC_FAMILY_NAME))
                .or_else(|| english_or_first_font_name(font_ref, StringId::FAMILY_NAME))
        })
        .unwrap_or_default()
}

fn postscript_name(font_ref: &BridgeFontRef, out_string: &mut String) -> bool {
    let postscript_name = english_or_first_font_name(font_ref, StringId::POSTSCRIPT_NAME);
    match postscript_name {
        Some(name) => {
            *out_string = name;
            true
        }
        _ => false,
    }
}

fn resolve_palette(
    font_ref: &BridgeFontRef,
    base_palette: u16,
    palette_overrides: &[PaletteOverride],
) -> Vec<u32> {
    let cpal_to_vector = |cpal: &Cpal, palette_index| -> Option<Vec<u32>> {
        let start_index: usize = cpal
            .color_record_indices()
            .get(usize::from(palette_index))?
            .get()
            .into();
        let num_entries: usize = cpal.num_palette_entries().into();
        let color_records = cpal.color_records_array()?.ok()?;
        Some(
            color_records
                .get(start_index..start_index + num_entries)?
                .iter()
                .map(|record| {
                    u32::from_be_bytes([record.alpha, record.red, record.green, record.blue])
                })
                .collect(),
        )
    };

    font_ref
        .with_font(|f| {
            let cpal = f.cpal().ok()?;

            let mut palette = cpal_to_vector(&cpal, base_palette).or(cpal_to_vector(&cpal, 0))?;

            for override_entry in palette_overrides {
                let index = override_entry.index as usize;
                if index < palette.len() {
                    palette[index] = override_entry.color_8888;
                }
            }
            Some(palette)
        })
        .unwrap_or_default()
}

fn has_colr_glyph(font_ref: &BridgeFontRef, format: ColorGlyphFormat, glyph_id: u16) -> bool {
    font_ref
        .with_font(|f| {
            let colrv1_paintable = f
                .color_glyphs()
                .get_with_format(GlyphId::from(glyph_id), format);
            Some(colrv1_paintable.is_some())
        })
        .unwrap_or_default()
}

fn has_colrv1_glyph(font_ref: &BridgeFontRef, glyph_id: u16) -> bool {
    has_colr_glyph(font_ref, ColorGlyphFormat::ColrV1, glyph_id)
}

fn has_colrv0_glyph(font_ref: &BridgeFontRef, glyph_id: u16) -> bool {
    has_colr_glyph(font_ref, ColorGlyphFormat::ColrV0, glyph_id)
}

fn get_colrv1_clip_box(
    font_ref: &BridgeFontRef,
    coords: &BridgeNormalizedCoords,
    glyph_id: u16,
    size: f32,
    clip_box: &mut ClipBox,
) -> bool {
    let size = match size {
        x if x == 0.0 => {
            return false;
        }
        _ => Size::new(size),
    };
    font_ref
        .with_font(|f| {
            match f
                .color_glyphs()
                .get_with_format(GlyphId::from(glyph_id), ColorGlyphFormat::ColrV1)?
                .bounding_box(coords.normalized_coords.coords(), size)
            {
                Some(bounding_box) => {
                    *clip_box = ClipBox {
                        x_min: bounding_box.x_min,
                        y_min: bounding_box.y_min,
                        x_max: bounding_box.x_max,
                        y_max: bounding_box.y_max,
                    };
                    Some(true)
                }
                _ => None,
            }
        })
        .unwrap_or_default()
}

/// Implements the behavior expected for `SkTypeface::getTableData`, compare
/// documentation for this method and the FreeType implementation in Skia.
/// * If the target data array is empty, do not copy any data into it, but
///   return the size of the table.
/// * If the target data buffer is shorted than from offset to the end of the
///   table, truncate the data.
/// * If offset is longer than the table's length, return 0.
fn table_data(font_ref: &BridgeFontRef, tag: u32, offset: usize, data: &mut [u8]) -> usize {
    let table_data = font_ref
        .with_font(|f| f.table_data(Tag::from_be_bytes(tag.to_be_bytes())))
        .unwrap_or_default();
    let table_data = table_data.as_ref();
    // Remaining table data size measured from offset to end, or 0 if offset is
    // too large.
    let mut to_copy_length = table_data.len().saturating_sub(offset);
    match data.len() {
        0 => to_copy_length,
        _ => {
            to_copy_length = to_copy_length.min(data.len());
            let table_offset_data = table_data
                .get(offset..offset + to_copy_length)
                .unwrap_or_default();
            data.get_mut(..table_offset_data.len())
                .map_or(0, |data_slice| {
                    data_slice.copy_from_slice(table_offset_data);
                    data_slice.len()
                })
        }
    }
}

fn table_tags(font_ref: &BridgeFontRef, tags: &mut [u32]) -> u16 {
    font_ref
        .with_font(|f| {
            let table_directory = &f.table_directory;
            let table_tags_iter = table_directory
                .table_records()
                .iter()
                .map(|table| u32::from_be_bytes(table.tag.get().into_bytes()));
            tags.iter_mut()
                .zip(table_tags_iter)
                .for_each(|(out_tag, table_tag)| *out_tag = table_tag);
            Some(table_directory.num_tables())
        })
        .unwrap_or_default()
}

fn variation_position(
    coords: &BridgeNormalizedCoords,
    coordinates: &mut [SkiaDesignCoordinate],
) -> isize {
    if !coordinates.is_empty() {
        if coords.filtered_user_coords.len() > coordinates.len() {
            return -1;
        }
        let skia_design_coordinates =
            coords
                .filtered_user_coords
                .iter()
                .map(|setting| SkiaDesignCoordinate {
                    axis: u32::from_be_bytes(setting.selector.into_bytes()),
                    value: setting.value,
                });
        for (i, coord) in skia_design_coordinates.enumerate() {
            coordinates[i] = coord;
        }
    }
    coords.filtered_user_coords.len().try_into().unwrap()
}

fn coordinates_for_shifted_named_instance_index(
    font_ref: &BridgeFontRef,
    shifted_index: u32,
    coords: &mut [SkiaDesignCoordinate],
) -> isize {
    font_ref
        .with_font(|f| {
            let fvar = f.fvar().ok()?;
            let instances = fvar.instances().ok()?;
            let index: usize = ((shifted_index >> 16) - 1).try_into().unwrap();
            let instance_coords = instances.get(index).ok()?.coordinates;

            if coords.len() != 0 {
                if coords.len() < instance_coords.len() {
                    return None;
                }
                let axis_coords = f.axes().iter().zip(instance_coords.iter()).enumerate();
                for (i, axis_coord) in axis_coords {
                    coords[i] = SkiaDesignCoordinate {
                        axis: u32::from_be_bytes(axis_coord.0.tag().to_be_bytes()),
                        value: axis_coord.1.get().to_f32(),
                    };
                }
            }

            Some(instance_coords.len() as isize)
        })
        .unwrap_or(-1)
}

fn num_axes(font_ref: &BridgeFontRef) -> usize {
    font_ref
        .with_font(|f| Some(f.axes().len()))
        .unwrap_or_default()
}

fn populate_axes(font_ref: &BridgeFontRef, mut axis_wrapper: Pin<&mut AxisWrapper>) -> isize {
    font_ref
        .with_font(|f| {
            let axes = f.axes();
            // Populate incoming allocated SkFontParameters::Variation::Axis[] only when a
            // buffer is passed.
            if axis_wrapper.as_ref().size() > 0 {
                for (i, axis) in axes.iter().enumerate() {
                    if !axis_wrapper.as_mut().populate_axis(
                        i,
                        u32::from_be_bytes(axis.tag().into_bytes()),
                        axis.min_value(),
                        axis.default_value(),
                        axis.max_value(),
                        axis.is_hidden(),
                    ) {
                        return None;
                    }
                }
            }
            isize::try_from(axes.len()).ok()
        })
        .unwrap_or(-1)
}

fn make_font_ref_internal<'a>(font_data: &'a [u8], index: u32) -> Result<FontRef<'a>, ReadError> {
    match FileRef::new(font_data) {
        Ok(file_ref) => match file_ref {
            FileRef::Font(font_ref) => {
                // Indices with the higher bits set are meaningful here and do not result in an
                // error, as they may refer to a named instance and are taken into account by the
                // Fontations typeface implementation,
                // compare `coordinates_for_shifted_named_instance_index()`.
                if index & 0xFFFF > 0 {
                    Err(ReadError::InvalidCollectionIndex(index))
                } else {
                    Ok(font_ref)
                }
            }
            FileRef::Collection(collection) => collection.get(index),
        },
        Err(e) => Err(e),
    }
}

fn make_font_ref<'a>(font_data: &'a [u8], index: u32) -> Box<BridgeFontRef<'a>> {
    let font = make_font_ref_internal(font_data, index).ok();
    let has_any_color = font
        .as_ref()
        .map(|f| {
            f.cbdt().is_ok() ||
            f.sbix().is_ok() ||
            // ColorGlyphCollection::get_with_format() first thing checks for presence of colr(),
            // so we do the same:
            f.colr().is_ok()
        })
        .unwrap_or_default();

    Box::new(BridgeFontRef {
        font,
        has_any_color,
    })
}

fn font_ref_is_valid(bridge_font_ref: &BridgeFontRef) -> bool {
    bridge_font_ref.font.is_some()
}

fn has_any_color_table(bridge_font_ref: &BridgeFontRef) -> bool {
    bridge_font_ref.has_any_color
}

fn get_outline_collection<'a>(font_ref: &'a BridgeFontRef<'a>) -> Box<BridgeOutlineCollection<'a>> {
    Box::new(
        font_ref
            .with_font(|f| Some(BridgeOutlineCollection(Some(f.outline_glyphs()))))
            .unwrap_or_default(),
    )
}

fn font_or_collection<'a>(font_data: &'a [u8], num_fonts: &mut u32) -> bool {
    match FileRef::new(font_data) {
        Ok(FileRef::Collection(collection)) => {
            *num_fonts = collection.len();
            true
        }
        Ok(FileRef::Font(_)) => {
            *num_fonts = 0u32;
            true
        }
        _ => false,
    }
}

fn num_named_instances(font_ref: &BridgeFontRef) -> usize {
    font_ref
        .with_font(|f| Some(f.named_instances().len()))
        .unwrap_or_default()
}

fn resolve_into_normalized_coords(
    font_ref: &BridgeFontRef,
    design_coords: &[SkiaDesignCoordinate],
) -> Box<BridgeNormalizedCoords> {
    let variation_tuples = design_coords
        .iter()
        .map(|coord| (Tag::from_be_bytes(coord.axis.to_be_bytes()), coord.value));
    let bridge_normalized_coords = font_ref
        .with_font(|f| {
            let merged_defaults_with_user = f
                .axes()
                .iter()
                .map(|axis| (axis.tag(), axis.default_value()))
                .chain(design_coords.iter().map(|user_coord| {
                    (
                        Tag::from_be_bytes(user_coord.axis.to_be_bytes()),
                        user_coord.value,
                    )
                }));
            Some(BridgeNormalizedCoords {
                filtered_user_coords: f.axes().filter(merged_defaults_with_user).collect(),
                normalized_coords: f.axes().location(variation_tuples),
            })
        })
        .unwrap_or_default();
    Box::new(bridge_normalized_coords)
}

fn normalized_coords_equal(a: &BridgeNormalizedCoords, b: &BridgeNormalizedCoords) -> bool {
    a.normalized_coords.coords() == b.normalized_coords.coords()
}

fn draw_colr_glyph(
    font_ref: &BridgeFontRef,
    coords: &BridgeNormalizedCoords,
    glyph_id: u16,
    color_painter: Pin<&mut ColorPainterWrapper>,
) -> bool {
    let mut color_painter_impl = ColorPainterImpl {
        color_painter_wrapper: color_painter,
        // In bounds mode, we do not need to track or forward to the client anything below the
        // first clip layer, as the bounds cannot grow after that.
        clip_level: 0,
    };
    font_ref
        .with_font(|f| {
            let paintable = f.color_glyphs().get(GlyphId::from(glyph_id))?;
            paintable
                .paint(coords.normalized_coords.coords(), &mut color_painter_impl)
                .ok()
        })
        .is_some()
}

fn next_color_stop(color_stops: &mut BridgeColorStops, out_stop: &mut ColorStop) -> bool {
    if let Some(color_stop) = color_stops.stops_iterator.next() {
        out_stop.alpha = color_stop.alpha;
        out_stop.stop = color_stop.offset;
        out_stop.palette_index = color_stop.palette_index;
        true
    } else {
        false
    }
}

fn num_color_stops(color_stops: &BridgeColorStops) -> usize {
    color_stops.num_stops
}

#[allow(non_upper_case_globals)]
fn get_font_style(
    font_ref: &BridgeFontRef,
    coords: &BridgeNormalizedCoords,
    style: &mut BridgeFontStyle,
) -> bool {
    const SKIA_SLANT_UPRIGHT: i32 = 0; /* kUpright_Slant */
    const SKIA_SLANT_ITALIC: i32 = 1; /* kItalic_Slant */
    const SKIA_SLANT_OBLIQUE: i32 = 2; /* kOblique_Slant */

    font_ref
        .with_font(|f| {
            let attrs = f.attributes();
            let mut skia_weight = attrs.weight.value().round() as i32;
            let mut skia_slant = match attrs.style {
                Style::Normal => SKIA_SLANT_UPRIGHT,
                Style::Italic => SKIA_SLANT_ITALIC,
                _ => SKIA_SLANT_OBLIQUE,
            };
            //0.5, 0.625, 0.75, 0.875, 1.0, 1.125, 1.25, 1.5, 2.0 map to 1-9
            let mut skia_width = match attrs.stretch.ratio() {
                x if x <= 0.5625 => 1,
                x if x <= 0.6875 => 2,
                x if x <= 0.8125 => 3,
                x if x <= 0.9375 => 4,
                x if x <= 1.0625 => 5,
                x if x <= 1.1875 => 6,
                x if x <= 1.3750 => 7,
                x if x <= 1.7500 => 8,
                _ => 9,
            };

            const wght: Tag = Tag::new(b"wght");
            const wdth: Tag = Tag::new(b"wdth");
            const slnt: Tag = Tag::new(b"slnt");

            for user_coord in coords.filtered_user_coords.iter() {
                match user_coord.selector {
                    wght => skia_weight = user_coord.value.round() as i32,
                    // 50, 62.5, 75, 87.5, 100, 112.5, 125, 150, 200 map to 1-9
                    wdth => {
                        skia_width = match user_coord.value {
                            x if x <= 56.25 => 1,
                            x if x <= 68.75 => 2,
                            x if x <= 81.25 => 3,
                            x if x <= 93.75 => 4,
                            x if x <= 106.25 => 5,
                            x if x <= 118.75 => 6,
                            x if x <= 137.50 => 7,
                            x if x <= 175.00 => 8,
                            _ => 9,
                        }
                    }
                    slnt => {
                        if skia_slant != SKIA_SLANT_ITALIC {
                            if user_coord.value == 0.0 {
                                skia_slant = SKIA_SLANT_UPRIGHT;
                            } else {
                                skia_slant = SKIA_SLANT_OBLIQUE
                            }
                        }
                    }
                    _ => (),
                }
            }

            *style = BridgeFontStyle {
                weight: skia_weight,
                slant: skia_slant,
                width: skia_width,
            };
            Some(true)
        })
        .unwrap_or_default()
}

fn is_embeddable(font_ref: &BridgeFontRef) -> bool {
    font_ref
        .with_font(|f| {
            let fs_type = f.os2().ok()?.fs_type();
            // https://learn.microsoft.com/en-us/typography/opentype/spec/os2#fstype
            // Bit 2 and bit 9 must be cleared, "Restricted License embedding" and
            // "Bitmap embedding only" must both be unset.
            // Implemented to match SkTypeface_FreeType::onGetAdvancedMetrics.
            Some(fs_type & 0x202 == 0)
        })
        .unwrap_or(true)
}

fn is_subsettable(font_ref: &BridgeFontRef) -> bool {
    font_ref
        .with_font(|f| {
            let fs_type = f.os2().ok()?.fs_type();
            // https://learn.microsoft.com/en-us/typography/opentype/spec/os2#fstype
            Some((fs_type & 0x100) == 0)
        })
        .unwrap_or(true)
}

fn is_fixed_pitch(font_ref: &BridgeFontRef) -> bool {
    font_ref
        .with_font(|f| {
            // Compare DWriteFontTypeface::onGetAdvancedMetrics().
            Some(
                f.post().ok()?.is_fixed_pitch() != 0
                    || f.hhea().ok()?.number_of_h_metrics() == 1,
            )
        })
        .unwrap_or_default()
}

fn is_serif_style(font_ref: &BridgeFontRef) -> bool {
    const FAMILY_TYPE_TEXT_AND_DISPLAY: u8 = 2;
    const SERIF_STYLE_COVE: u8 = 2;
    const SERIF_STYLE_TRIANGLE: u8 = 10;
    font_ref
        .with_font(|f| {
            // Compare DWriteFontTypeface::onGetAdvancedMetrics().
            let panose = f.os2().ok()?.panose_10();
            let family_type = panose[0];

            match family_type {
                FAMILY_TYPE_TEXT_AND_DISPLAY => {
                    let serif_style = panose[1];
                    Some((SERIF_STYLE_COVE..=SERIF_STYLE_TRIANGLE).contains(&serif_style))
                }
                _ => None,
            }
        })
        .unwrap_or_default()
}

fn is_script_style(font_ref: &BridgeFontRef) -> bool {
    const FAMILY_TYPE_SCRIPT: u8 = 3;
    font_ref
        .with_font(|f| {
            // Compare DWriteFontTypeface::onGetAdvancedMetrics().
            let family_type = f.os2().ok()?.panose_10()[0];
            Some(family_type == FAMILY_TYPE_SCRIPT)
        })
        .unwrap_or_default()
}

fn italic_angle(font_ref: &BridgeFontRef) -> i32 {
    font_ref
        .with_font(|f| Some(f.post().ok()?.italic_angle().to_i32()))
        .unwrap_or_default()
}

pub struct BridgeFontRef<'a> {
    font: Option<FontRef<'a>>,
    has_any_color: bool,
}

impl<'a> BridgeFontRef<'a> {
    fn with_font<T>(&'a self, f: impl FnOnce(&'a FontRef) -> Option<T>) -> Option<T> {
        f(self.font.as_ref()?)
    }
}

#[derive(Default)]
struct BridgeOutlineCollection<'a>(Option<OutlineGlyphCollection<'a>>);

#[derive(Default)]
struct BridgeNormalizedCoords {
    normalized_coords: Location,
    filtered_user_coords: Vec<VariationSetting>,
}

struct BridgeLocalizedStrings<'a> {
    #[allow(dead_code)]
    localized_strings: LocalizedStrings<'a>,
}

pub struct BridgeColorStops<'a> {
    pub stops_iterator: Box<dyn Iterator<Item = &'a skrifa::color::ColorStop> + 'a>,
    pub num_stops: usize,
}

mod bitmap {

    use read_fonts::{
        tables::{
            bitmap::{BitmapContent, BitmapData, BitmapDataFormat, BitmapMetrics, BitmapSize},
            sbix::{GlyphData, Strike},
        },
        FontRef, TableProvider,
    };

    use font_types::{BoundingBox, GlyphId};
    use skrifa::{
        instance::{LocationRef, Size},
        metrics::GlyphMetrics,
    };

    use crate::{ffi::BitmapMetrics as FfiBitmapMetrics, BridgeFontRef};

    pub enum BitmapPixelData<'a> {
        PngData(&'a [u8]),
    }

    struct CblcGlyph<'a> {
        bitmap_data: BitmapData<'a>,
        ppem_x: u8,
        ppem_y: u8,
    }

    struct SbixGlyph<'a> {
        glyph_data: GlyphData<'a>,
        ppem: u16,
    }

    #[derive(Default)]
    pub struct BridgeBitmapGlyph<'a> {
        pub data: Option<BitmapPixelData<'a>>,
        pub metrics: FfiBitmapMetrics,
    }

    trait StrikeSizeRetrievable {
        fn strike_size(&self) -> f32;
    }

    impl StrikeSizeRetrievable for &BitmapSize {
        fn strike_size(&self) -> f32 {
            self.ppem_y() as f32
        }
    }

    impl StrikeSizeRetrievable for Strike<'_> {
        fn strike_size(&self) -> f32 {
            self.ppem() as f32
        }
    }

    // Find the nearest larger strike size, or if no larger one is available, the nearest smaller.
    fn best_strike_size<T>(strikes: impl Iterator<Item = T>, font_size: f32) -> Option<T>
    where
        T: StrikeSizeRetrievable,
    {
        // After a bigger strike size is found, the order of strike sizes smaller
        // than the requested font size does not matter anymore. A new strike size
        // is only an improvement if it gets closer to the requested font size (and
        // is smaller than the current best, but bigger than font size). And vice
        // versa: As long as we have found only smaller ones so far, only any strike
        // size matters that is bigger than the current best.
        strikes.reduce(|best, entry| {
            let entry_size = entry.strike_size();
            if (entry_size >= font_size && entry_size < best.strike_size())
                || (best.strike_size() < font_size && entry_size > best.strike_size())
            {
                entry
            } else {
                best
            }
        })
    }

    fn sbix_glyph<'a>(
        font_ref: &'a FontRef,
        glyph_id: GlyphId,
        font_size: Option<f32>,
    ) -> Option<SbixGlyph<'a>> {
        let sbix = font_ref.sbix().ok()?;
        let mut strikes = sbix.strikes().iter().filter_map(|strike| strike.ok());

        let best_strike = match font_size {
            Some(size) => best_strike_size(strikes, size),
            _ => strikes.next(),
        }?;

        Some(SbixGlyph {
            ppem: best_strike.ppem(),
            glyph_data: best_strike.glyph_data(glyph_id).ok()??,
        })
    }

    fn cblc_glyph<'a>(
        font_ref: &'a FontRef,
        glyph_id: GlyphId,
        font_size: Option<f32>,
    ) -> Option<CblcGlyph<'a>> {
        let cblc = font_ref.cblc().ok()?;
        let cbdt = font_ref.cbdt().ok()?;

        let strikes = &cblc.bitmap_sizes();
        let best_strike = font_size
            .and_then(|size| best_strike_size(strikes.iter(), size))
            .or(strikes.get(0))?;

        let location = best_strike.location(cblc.offset_data(), glyph_id).ok()?;

        Some(CblcGlyph {
            bitmap_data: cbdt.data(&location).ok()?,
            ppem_x: best_strike.ppem_x,
            ppem_y: best_strike.ppem_y,
        })
    }

    pub fn has_bitmap_glyph(font_ref: &BridgeFontRef, glyph_id: u16) -> bool {
        font_ref
            .with_font(|font| {
                let glyph_id = GlyphId::from(glyph_id);
                let has_sbix = sbix_glyph(font, glyph_id, None).is_some();
                let has_cblc = cblc_glyph(font, glyph_id, None).is_some();
                Some(has_sbix || has_cblc)
            })
            .unwrap_or_default()
    }

    fn glyf_bounds(font_ref: &FontRef, glyph_id: GlyphId) -> Option<BoundingBox<i16>> {
        let glyf_table = font_ref.glyf().ok()?;
        let glyph = font_ref
            .loca(None)
            .ok()?
            .get_glyf(glyph_id, &glyf_table)
            .ok()??;
        Some(BoundingBox {
            x_min: glyph.x_min(),
            y_min: glyph.y_min(),
            x_max: glyph.x_max(),
            y_max: glyph.y_max(),
        })
    }

    pub unsafe fn bitmap_glyph<'a>(
        font_ref: &'a BridgeFontRef,
        glyph_id: u16,
        font_size: f32,
    ) -> Box<BridgeBitmapGlyph<'a>> {
        let glyph_id = GlyphId::from(glyph_id);
        font_ref
            .with_font(|font| {
                if let Some(sbix_glyph) = sbix_glyph(font, glyph_id, Some(font_size)) {
                    // https://learn.microsoft.com/en-us/typography/opentype/spec/sbix
                    // "If there is a glyph contour, the glyph design space
                    // origin for the graphic is placed at the lower left corner
                    // of the glyph bounding box (xMin, yMin)."
                    let glyf_bb = glyf_bounds(font, glyph_id).unwrap_or_default();
                    let glyf_left_side_bearing =
                        GlyphMetrics::new(font, Size::unscaled(), LocationRef::default())
                            .left_side_bearing(glyph_id)
                            .unwrap_or_default();

                    return Some(Box::new(BridgeBitmapGlyph {
                        data: Some(BitmapPixelData::PngData(sbix_glyph.glyph_data.data())),
                        metrics: FfiBitmapMetrics {
                            bearing_x: glyf_left_side_bearing,
                            bearing_y: glyf_bb.y_min as f32,
                            inner_bearing_x: sbix_glyph.glyph_data.origin_offset_x() as f32,
                            inner_bearing_y: sbix_glyph.glyph_data.origin_offset_y() as f32,
                            ppem_x: sbix_glyph.ppem as f32,
                            ppem_y: sbix_glyph.ppem as f32,
                            placement_origin_bottom_left: true,
                            advance: f32::NAN,
                        },
                    }));
                } else if let Some(cblc_glyph) = cblc_glyph(font, glyph_id, Some(font_size)) {
                    let (bearing_x, bearing_y, advance) = match cblc_glyph.bitmap_data.metrics {
                        BitmapMetrics::Small(small_metrics) => (
                            small_metrics.bearing_x() as f32,
                            small_metrics.bearing_y() as f32,
                            small_metrics.advance as f32,
                        ),
                        BitmapMetrics::Big(big_metrics) => (
                            big_metrics.hori_bearing_x() as f32,
                            big_metrics.hori_bearing_y() as f32,
                            big_metrics.hori_advance as f32,
                        ),
                    };
                    if let BitmapContent::Data(BitmapDataFormat::Png, png_buffer) =
                        cblc_glyph.bitmap_data.content
                    {
                        return Some(Box::new(BridgeBitmapGlyph {
                            data: Some(BitmapPixelData::PngData(png_buffer)),
                            metrics: FfiBitmapMetrics {
                                bearing_x: 0.0,
                                bearing_y: 0.0,
                                inner_bearing_x: bearing_x,
                                inner_bearing_y: bearing_y,
                                ppem_x: cblc_glyph.ppem_x as f32,
                                ppem_y: cblc_glyph.ppem_y as f32,
                                placement_origin_bottom_left: false,
                                advance: advance,
                            },
                        }));
                    }
                }
                None
            })
            .unwrap_or_default()
    }

    pub unsafe fn png_data<'a>(bitmap_glyph: &'a BridgeBitmapGlyph) -> &'a [u8] {
        match bitmap_glyph.data {
            Some(BitmapPixelData::PngData(glyph_data)) => glyph_data,
            _ => &[],
        }
    }

    pub unsafe fn bitmap_metrics<'a>(bitmap_glyph: &'a BridgeBitmapGlyph) -> &'a FfiBitmapMetrics {
        &bitmap_glyph.metrics
    }
}

pub struct BridgeMappingIndex(MappingIndex);
pub struct BridgeHintingInstance(Option<HintingInstance>);

#[cxx::bridge(namespace = "fontations_ffi")]
mod ffi {
    struct ColorStop {
        stop: f32,
        palette_index: u16,
        alpha: f32,
    }

    #[derive(Default)]
    struct Metrics {
        top: f32,
        ascent: f32,
        descent: f32,
        bottom: f32,
        leading: f32,
        avg_char_width: f32,
        max_char_width: f32,
        x_min: f32,
        x_max: f32,
        x_height: f32,
        cap_height: f32,
        underline_position: f32,
        underline_thickness: f32,
        strikeout_position: f32,
        strikeout_thickness: f32,
    }

    #[derive(Clone, Copy, Default, PartialEq)]
    struct FfiPoint {
        x: f32,
        y: f32,
    }

    struct BridgeLocalizedName {
        string: String,
        language: String,
    }

    #[derive(PartialEq, Debug, Default)]
    struct SkiaDesignCoordinate {
        axis: u32,
        value: f32,
    }

    struct BridgeScalerMetrics {
        has_overlaps: bool,
    }

    struct PaletteOverride {
        index: u16,
        color_8888: u32,
    }

    struct ClipBox {
        x_min: f32,
        y_min: f32,
        x_max: f32,
        y_max: f32,
    }

    struct Transform {
        xx: f32,
        xy: f32,
        yx: f32,
        yy: f32,
        dx: f32,
        dy: f32,
    }

    struct FillLinearParams {
        x0: f32,
        y0: f32,
        x1: f32,
        y1: f32,
    }

    struct FillRadialParams {
        x0: f32,
        y0: f32,
        r0: f32,
        x1: f32,
        y1: f32,
        r1: f32,
    }

    struct FillSweepParams {
        x0: f32,
        y0: f32,
        start_angle: f32,
        end_angle: f32,
    }

    // This type is used to mirror SkFontStyle values for Weight, Slant and Width
    #[derive(Default)]
    pub struct BridgeFontStyle {
        pub weight: i32,
        pub slant: i32,
        pub width: i32,
    }

    #[derive(Default)]
    struct BitmapMetrics {
        // Outer glyph bearings that affect the computed bounds. We distinguish
        // those here from `inner_bearing_*` to account for CoreText behavior in
        // SBIX placement. Where the sbix originOffsetX/Y are applied only
        // within the bounds. Specified in font units.
        // 0 for CBDT, CBLC.
        bearing_x: f32,
        bearing_y: f32,
        // Scale factors to scale image to 1em.
        ppem_x: f32,
        ppem_y: f32,
        // Account for the fact that Sbix and CBDT/CBLC have a different origin
        // definition.
        placement_origin_bottom_left: bool,
        // Specified as a pixel value, to be scaled by `ppem_*` as an
        // offset applied to placing the image within the bounds rectangle.
        inner_bearing_x: f32,
        inner_bearing_y: f32,
        // Some, but not all, bitmap glyphs have a special bitmap advance
        advance: f32,
    }

    enum AutoHintingControl {
        PreferAutoOverHintsForGlyf,
        ForceForGlyfAndCff,
        AutoAsFallback,
    }

    extern "Rust" {
        type BridgeFontRef<'a>;
        unsafe fn make_font_ref<'a>(font_data: &'a [u8], index: u32) -> Box<BridgeFontRef<'a>>;
        // Returns whether BridgeFontRef is a valid font containing at
        // least a valid sfnt structure from which tables can be
        // accessed. This is what instantiation in make_font_ref checks
        // for. (see FontRef::new in read_fonts's lib.rs). Implemented
        // by returning whether the option is Some() and thus whether a
        // FontRef instantiation succeeded and a table directory was
        // accessible.
        fn font_ref_is_valid(bridge_font_ref: &BridgeFontRef) -> bool;

        // Optimization to quickly rule out that the font has any color tables.
        fn has_any_color_table(bridge_font_ref: &BridgeFontRef) -> bool;

        type BridgeOutlineCollection<'a>;
        unsafe fn get_outline_collection<'a>(
            font_ref: &'a BridgeFontRef<'a>,
        ) -> Box<BridgeOutlineCollection<'a>>;

        /// Returns true on a font or collection, sets `num_fonts``
        /// to 0 if single font file, and to > 0 for a TrueType collection.
        /// Returns false if the data cannot be interpreted as a font or collection.
        unsafe fn font_or_collection<'a>(font_data: &'a [u8], num_fonts: &mut u32) -> bool;

        unsafe fn num_named_instances(font_ref: &BridgeFontRef) -> usize;

        type BridgeMappingIndex;
        unsafe fn make_mapping_index<'a>(font_ref: &'a BridgeFontRef) -> Box<BridgeMappingIndex>;

        unsafe fn hinting_reliant<'a>(font_ref: &'a BridgeOutlineCollection) -> bool;

        type BridgeHintingInstance;
        unsafe fn make_hinting_instance<'a>(
            outlines: &BridgeOutlineCollection,
            size: f32,
            coords: &BridgeNormalizedCoords,
            do_light_hinting: bool,
            do_lcd_antialiasing: bool,
            lcd_orientation_vertical: bool,
            autohinting_control: AutoHintingControl,
        ) -> Box<BridgeHintingInstance>;
        unsafe fn make_mono_hinting_instance<'a>(
            outlines: &BridgeOutlineCollection,
            size: f32,
            coords: &BridgeNormalizedCoords,
        ) -> Box<BridgeHintingInstance>;
        unsafe fn no_hinting_instance<'a>() -> Box<BridgeHintingInstance>;

        fn lookup_glyph_or_zero(
            font_ref: &BridgeFontRef,
            map: &BridgeMappingIndex,
            codepoint: &[u32],
            glyphs: &mut [u16],
        );

        fn get_path_verbs_points(
            outlines: &BridgeOutlineCollection,
            glyph_id: u16,
            size: f32,
            coords: &BridgeNormalizedCoords,
            hinting_instance: &BridgeHintingInstance,
            verbs: &mut Vec<u8>,
            points: &mut Vec<FfiPoint>,
            scaler_metrics: &mut BridgeScalerMetrics,
        ) -> bool;

        fn shrink_verbs_points_if_needed(verbs: &mut Vec<u8>, points: &mut Vec<FfiPoint>);

        fn unhinted_advance_width_or_zero(
            font_ref: &BridgeFontRef,
            size: f32,
            coords: &BridgeNormalizedCoords,
            glyph_id: u16,
        ) -> f32;
        fn scaler_hinted_advance_width(
            outlines: &BridgeOutlineCollection,
            hinting_instance: &BridgeHintingInstance,
            glyph_id: u16,
            out_advance_width: &mut f32,
        ) -> bool;
        fn units_per_em_or_zero(font_ref: &BridgeFontRef) -> u16;
        fn get_skia_metrics(
            font_ref: &BridgeFontRef,
            size: f32,
            coords: &BridgeNormalizedCoords,
        ) -> Metrics;
        fn get_unscaled_metrics(
            font_ref: &BridgeFontRef,
            coords: &BridgeNormalizedCoords,
        ) -> Metrics;
        fn num_glyphs(font_ref: &BridgeFontRef) -> u16;
        fn fill_glyph_to_unicode_map(font_ref: &BridgeFontRef, map: &mut [u32]);
        fn family_name(font_ref: &BridgeFontRef) -> String;
        fn postscript_name(font_ref: &BridgeFontRef, out_string: &mut String) -> bool;

        /// Receives a slice of palette overrides that will be merged
        /// with the specified base palette of the font. The result is a
        /// palette of RGBA, 8-bit per component, colors, consisting of
        /// palette entries merged with overrides.
        fn resolve_palette(
            font_ref: &BridgeFontRef,
            base_palette: u16,
            palette_overrides: &[PaletteOverride],
        ) -> Vec<u32>;

        fn has_colrv1_glyph(font_ref: &BridgeFontRef, glyph_id: u16) -> bool;
        fn has_colrv0_glyph(font_ref: &BridgeFontRef, glyph_id: u16) -> bool;
        fn get_colrv1_clip_box(
            font_ref: &BridgeFontRef,
            coords: &BridgeNormalizedCoords,
            glyph_id: u16,
            size: f32,
            clip_box: &mut ClipBox,
        ) -> bool;

        type BridgeBitmapGlyph<'a>;
        fn has_bitmap_glyph(font_ref: &BridgeFontRef, glyph_id: u16) -> bool;
        unsafe fn bitmap_glyph<'a>(
            font_ref: &'a BridgeFontRef,
            glyph_id: u16,
            font_size: f32,
        ) -> Box<BridgeBitmapGlyph<'a>>;
        unsafe fn png_data<'a>(bitmap_glyph: &'a BridgeBitmapGlyph) -> &'a [u8];
        unsafe fn bitmap_metrics<'a>(bitmap_glyph: &'a BridgeBitmapGlyph) -> &'a BitmapMetrics;

        fn table_data(font_ref: &BridgeFontRef, tag: u32, offset: usize, data: &mut [u8]) -> usize;
        fn table_tags(font_ref: &BridgeFontRef, tags: &mut [u32]) -> u16;
        fn variation_position(
            coords: &BridgeNormalizedCoords,
            coordinates: &mut [SkiaDesignCoordinate],
        ) -> isize;
        // Fills the passed-in slice with the axis coordinates for a given
        // shifted named instance index. A shifted named instance index is a
        // 32bit value that contains the index to a named instance left-shifted
        // by 16bits and offset by 1. This mirrors FreeType behavior to smuggle
        // named instance identifiers through a TrueType collection index.
        // Returns the number of coordinates copied. If the slice length is 0,
        // performs no copy but only returns the number of axis coordinates for
        // the given shifted index. Returns -1 on error.
        fn coordinates_for_shifted_named_instance_index(
            font_ref: &BridgeFontRef,
            shifted_index: u32,
            coords: &mut [SkiaDesignCoordinate],
        ) -> isize;

        fn num_axes(font_ref: &BridgeFontRef) -> usize;

        fn populate_axes(font_ref: &BridgeFontRef, axis_wrapper: Pin<&mut AxisWrapper>) -> isize;

        type BridgeLocalizedStrings<'a>;
        unsafe fn get_localized_strings<'a>(
            font_ref: &'a BridgeFontRef<'a>,
        ) -> Box<BridgeLocalizedStrings<'a>>;
        fn localized_name_next(
            bridge_localized_strings: &mut BridgeLocalizedStrings,
            out_localized_name: &mut BridgeLocalizedName,
        ) -> bool;

        type BridgeNormalizedCoords;
        fn resolve_into_normalized_coords(
            font_ref: &BridgeFontRef,
            design_coords: &[SkiaDesignCoordinate],
        ) -> Box<BridgeNormalizedCoords>;

        fn normalized_coords_equal(a: &BridgeNormalizedCoords, b: &BridgeNormalizedCoords) -> bool;

        fn draw_colr_glyph(
            font_ref: &BridgeFontRef,
            coords: &BridgeNormalizedCoords,
            glyph_id: u16,
            color_painter: Pin<&mut ColorPainterWrapper>,
        ) -> bool;

        type BridgeColorStops<'a>;
        fn next_color_stop(color_stops: &mut BridgeColorStops, stop: &mut ColorStop) -> bool;
        fn num_color_stops(color_stops: &BridgeColorStops) -> usize;

        fn get_font_style(
            font_ref: &BridgeFontRef,
            coords: &BridgeNormalizedCoords,
            font_style: &mut BridgeFontStyle,
        ) -> bool;

        // Additional low-level access functions needed for generateAdvancedMetrics().
        fn is_embeddable(font_ref: &BridgeFontRef) -> bool;
        fn is_subsettable(font_ref: &BridgeFontRef) -> bool;
        fn is_fixed_pitch(font_ref: &BridgeFontRef) -> bool;
        fn is_serif_style(font_ref: &BridgeFontRef) -> bool;
        fn is_script_style(font_ref: &BridgeFontRef) -> bool;
        fn italic_angle(font_ref: &BridgeFontRef) -> i32;
    }

    unsafe extern "C++" {

        include!("src/ports/fontations/src/skpath_bridge.h");

        type AxisWrapper;

        fn populate_axis(
            self: Pin<&mut AxisWrapper>,
            i: usize,
            axis: u32,
            min: f32,
            def: f32,
            max: f32,
            hidden: bool,
        ) -> bool;
        fn size(self: Pin<&AxisWrapper>) -> usize;

        type ColorPainterWrapper;

        fn is_bounds_mode(self: Pin<&mut ColorPainterWrapper>) -> bool;
        fn push_transform(self: Pin<&mut ColorPainterWrapper>, transform: &Transform);
        fn pop_transform(self: Pin<&mut ColorPainterWrapper>);
        fn push_clip_glyph(self: Pin<&mut ColorPainterWrapper>, glyph_id: u16);
        fn push_clip_rectangle(
            self: Pin<&mut ColorPainterWrapper>,
            x_min: f32,
            y_min: f32,
            x_max: f32,
            y_max: f32,
        );
        fn pop_clip(self: Pin<&mut ColorPainterWrapper>);

        fn fill_solid(self: Pin<&mut ColorPainterWrapper>, palette_index: u16, alpha: f32);
        fn fill_linear(
            self: Pin<&mut ColorPainterWrapper>,
            fill_linear_params: &FillLinearParams,
            color_stops: &mut BridgeColorStops,
            extend_mode: u8,
        );
        fn fill_radial(
            self: Pin<&mut ColorPainterWrapper>,
            fill_radial_params: &FillRadialParams,
            color_stops: &mut BridgeColorStops,
            extend_mode: u8,
        );
        fn fill_sweep(
            self: Pin<&mut ColorPainterWrapper>,
            fill_sweep_params: &FillSweepParams,
            color_stops: &mut BridgeColorStops,
            extend_mode: u8,
        );

        // Optimized functions.
        fn fill_glyph_solid(
            self: Pin<&mut ColorPainterWrapper>,
            glyph_id: u16,
            palette_index: u16,
            alpha: f32,
        );
        fn fill_glyph_linear(
            self: Pin<&mut ColorPainterWrapper>,
            glyph_id: u16,
            fill_transform: &Transform,
            fill_linear_params: &FillLinearParams,
            color_stops: &mut BridgeColorStops,
            extend_mode: u8,
        );
        fn fill_glyph_radial(
            self: Pin<&mut ColorPainterWrapper>,
            glyph_id: u16,
            fill_transform: &Transform,
            fill_radial_params: &FillRadialParams,
            color_stops: &mut BridgeColorStops,
            extend_mode: u8,
        );
        fn fill_glyph_sweep(
            self: Pin<&mut ColorPainterWrapper>,
            glyph_id: u16,
            fill_transform: &Transform,
            fill_sweep_params: &FillSweepParams,
            color_stops: &mut BridgeColorStops,
            extend_mode: u8,
        );

        fn push_layer(self: Pin<&mut ColorPainterWrapper>, colrv1_composite_mode: u8);
        fn pop_layer(self: Pin<&mut ColorPainterWrapper>);

    }
}

/// Tests to exercise COLR and CPAL parts of the Fontations FFI.
/// Run using `$ bazel test --with_fontations //src/ports/fontations:test_ffi`
#[cfg(test)]
mod test {
    use crate::{
        coordinates_for_shifted_named_instance_index,
        ffi::{BridgeFontStyle, PaletteOverride, SkiaDesignCoordinate},
        font_or_collection, font_ref_is_valid, get_font_style, make_font_ref, num_axes,
        num_named_instances, resolve_into_normalized_coords, resolve_palette,
    };
    use std::fs;

    const TEST_FONT_FILENAME: &str = "resources/fonts/test_glyphs-glyf_colr_1_variable.ttf";
    const TEST_COLLECTION_FILENAME: &str = "resources/fonts/test.ttc";
    const TEST_CONDENSED_BOLD_ITALIC: &str = "resources/fonts/cond-bold-italic.ttf";
    const TEST_VARIABLE: &str = "resources/fonts/Variable.ttf";

    #[test]
    fn test_palette_override() {
        let file_buffer =
            fs::read(TEST_FONT_FILENAME).expect("COLRv0/v1 test font could not be opened.");
        let font_ref = make_font_ref(&file_buffer, 0);
        assert!(font_ref_is_valid(&font_ref));

        let override_color = 0xFFEEEEEE;
        let valid_overrides = [
            PaletteOverride {
                index: 9,
                color_8888: override_color,
            },
            PaletteOverride {
                index: 10,
                color_8888: override_color,
            },
            PaletteOverride {
                index: 11,
                color_8888: override_color,
            },
        ];

        let palette = resolve_palette(&font_ref, 0, &valid_overrides);

        assert_eq!(palette.len(), 14);
        assert_eq!(palette[9], override_color);
        assert_eq!(palette[10], override_color);
        assert_eq!(palette[11], override_color);

        let out_of_bounds_overrides = [
            PaletteOverride {
                index: 15,
                color_8888: override_color,
            },
            PaletteOverride {
                index: 16,
                color_8888: override_color,
            },
            PaletteOverride {
                index: 17,
                color_8888: override_color,
            },
        ];

        let palette = resolve_palette(&font_ref, 0, &out_of_bounds_overrides);

        assert_eq!(palette.len(), 14);
        assert_eq!(
            (palette[11], palette[12], palette[13],),
            (0xff68c7e8, 0xffffdc01, 0xff808080)
        );
    }

    #[test]
    fn test_default_palette_for_invalid_index() {
        let file_buffer =
            fs::read(TEST_FONT_FILENAME).expect("COLRv0/v1 test font could not be opened.");
        let font_ref = make_font_ref(&file_buffer, 0);
        assert!(font_ref_is_valid(&font_ref));
        let palette = resolve_palette(&font_ref, 65535, &[]);
        assert_eq!(palette.len(), 14);
        assert_eq!(
            (palette[0], palette[6], palette[13],),
            (0xFFFF0000, 0xFFEE82EE, 0xFF808080)
        );
    }

    #[test]
    fn test_num_fonts_in_collection() {
        let collection_buffer = fs::read(TEST_COLLECTION_FILENAME)
            .expect("Unable to open TrueType collection test file.");
        let font_buffer =
            fs::read(TEST_FONT_FILENAME).expect("COLRv0/v1 test font could not be opened.");
        let garbage: [u8; 12] = [
            b'0', b'a', b'b', b'0', b'a', b'b', b'0', b'a', b'b', b'0', b'a', b'b',
        ];

        let mut num_fonts = 0;
        let result_collection = font_or_collection(&collection_buffer, &mut num_fonts);
        assert!(result_collection && num_fonts == 2);

        let result_font_file = font_or_collection(&font_buffer, &mut num_fonts);
        assert!(result_font_file);
        assert!(num_fonts == 0u32);

        let result_garbage = font_or_collection(&garbage, &mut num_fonts);
        assert!(!result_garbage);
    }

    #[test]
    fn test_font_attributes() {
        let file_buffer = fs::read(TEST_CONDENSED_BOLD_ITALIC)
            .expect("Font to test font styles could not be opened.");
        let font_ref = make_font_ref(&file_buffer, 0);
        let coords = resolve_into_normalized_coords(&font_ref, &[]);
        assert!(font_ref_is_valid(&font_ref));

        let mut font_style = BridgeFontStyle::default();

        if get_font_style(font_ref.as_ref(), &coords, &mut font_style) {
            assert_eq!(font_style.width, 5); // The font should have condenced width attribute but
                                             // it's condenced itself so we have the normal width
            assert_eq!(font_style.slant, 1); // Skia italic
            assert_eq!(font_style.weight, 700); // Skia bold
        } else {
            assert!(false);
        }
    }

    #[test]
    fn test_variable_font_attributes() {
        let file_buffer =
            fs::read(TEST_VARIABLE).expect("Font to test font styles could not be opened.");
        let font_ref = make_font_ref(&file_buffer, 0);
        let coords = resolve_into_normalized_coords(&font_ref, &[]);
        assert!(font_ref_is_valid(&font_ref));

        let mut font_style = BridgeFontStyle::default();

        assert!(get_font_style(font_ref.as_ref(), &coords, &mut font_style));
        assert_eq!(font_style.width, 5); // Skia normal
        assert_eq!(font_style.slant, 0); // Skia upright
        assert_eq!(font_style.weight, 400); // Skia normal
    }

    #[test]
    fn test_no_instances() {
        let font_buffer = fs::read(TEST_CONDENSED_BOLD_ITALIC)
            .expect("Font to test font styles could not be opened.");
        let font_ref = make_font_ref(&font_buffer, 0);
        let num_instances = num_named_instances(font_ref.as_ref());
        assert!(num_instances == 0);
    }

    #[test]
    fn test_no_axes() {
        let font_buffer = fs::read(TEST_CONDENSED_BOLD_ITALIC)
            .expect("Font to test font styles could not be opened.");
        let font_ref = make_font_ref(&font_buffer, 0);
        let size = num_axes(&font_ref);
        assert_eq!(0, size);
    }

    #[test]
    fn test_named_instances() {
        let font_buffer =
            fs::read(TEST_VARIABLE).expect("Font to test font styles could not be opened.");

        let font_ref = make_font_ref(&font_buffer, 0);
        let num_instances = num_named_instances(font_ref.as_ref());
        assert!(num_instances == 5);

        let mut index = 0;
        loop {
            if index >= num_instances {
                break;
            }
            let named_instance_index: u32 = ((index + 1) << 16) as u32;
            let num_coords = coordinates_for_shifted_named_instance_index(
                &font_ref,
                named_instance_index,
                &mut [],
            );
            assert_eq!(num_coords, 2);

            let mut received_coords: [SkiaDesignCoordinate; 2] = Default::default();
            let num_coords = coordinates_for_shifted_named_instance_index(
                &font_ref,
                named_instance_index,
                &mut received_coords,
            );
            let size = num_axes(&font_ref) as isize;
            assert_eq!(num_coords, size);
            if (index + 1) == 5 {
                assert_eq!(num_coords, 2);
                assert_eq!(
                    received_coords[0],
                    SkiaDesignCoordinate {
                        axis: u32::from_be_bytes([b'w', b'g', b'h', b't']),
                        value: 400.0
                    }
                );
                assert_eq!(
                    received_coords[1],
                    SkiaDesignCoordinate {
                        axis: u32::from_be_bytes([b'w', b'd', b't', b'h']),
                        value: 200.0
                    }
                );
            };
            index += 1;
        }
    }

    #[test]
    fn test_shifted_named_instance_index() {
        let file_buffer =
            fs::read(TEST_VARIABLE).expect("Font to test named instances could not be opened.");
        let font_ref = make_font_ref(&file_buffer, 0);
        assert!(font_ref_is_valid(&font_ref));
        // Named instances are 1-indexed.
        const SHIFTED_NAMED_INSTANCE_INDEX: u32 = 5 << 16;
        const OUT_OF_BOUNDS_NAMED_INSTANCE_INDEX: u32 = 6 << 16;

        let num_coords = coordinates_for_shifted_named_instance_index(
            &font_ref,
            SHIFTED_NAMED_INSTANCE_INDEX,
            &mut [],
        );
        assert_eq!(num_coords, 2);

        let mut too_small: [SkiaDesignCoordinate; 1] = Default::default();
        let num_coords = coordinates_for_shifted_named_instance_index(
            &font_ref,
            SHIFTED_NAMED_INSTANCE_INDEX,
            &mut too_small,
        );
        assert_eq!(num_coords, -1);

        let mut received_coords: [SkiaDesignCoordinate; 2] = Default::default();
        let num_coords = coordinates_for_shifted_named_instance_index(
            &font_ref,
            SHIFTED_NAMED_INSTANCE_INDEX,
            &mut received_coords,
        );
        assert_eq!(num_coords, 2);
        assert_eq!(
            received_coords[0],
            SkiaDesignCoordinate {
                axis: u32::from_be_bytes([b'w', b'g', b'h', b't']),
                value: 400.0
            }
        );
        assert_eq!(
            received_coords[1],
            SkiaDesignCoordinate {
                axis: u32::from_be_bytes([b'w', b'd', b't', b'h']),
                value: 200.0
            }
        );

        let mut too_large: [SkiaDesignCoordinate; 5] = Default::default();
        let num_coords = coordinates_for_shifted_named_instance_index(
            &font_ref,
            SHIFTED_NAMED_INSTANCE_INDEX,
            &mut too_large,
        );
        assert_eq!(num_coords, 2);

        // Index out of bounds:
        let num_coords = coordinates_for_shifted_named_instance_index(
            &font_ref,
            OUT_OF_BOUNDS_NAMED_INSTANCE_INDEX,
            &mut [],
        );
        assert_eq!(num_coords, -1);
    }
}
