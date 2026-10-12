#include "border.h"
#include "hashtable.h"
#include "misc/extern.h"
#include "misc/yabai_sa.h"
#include "windows.h"
#include <pthread.h>
#include <time.h>

extern struct settings g_settings;

struct settings* border_get_settings(struct border* border) {
  assert(pthread_main_np() != 0);
  return border->setting_override.enabled
         ? &border->setting_override
         : &g_settings;
}

static void border_destroy_window(struct border* border) {
  for (int i = 0; i < BORDER_EDGE_COUNT; i++) {
    struct border_edge* edge = &border->edges[i];
    if (edge->grouped) yabai_sa_ordering_group(border->target_wid, edge->wid, 0);
    if (edge->context) CGContextRelease(edge->context);
    if (edge->wid) SLSReleaseWindow(border->cid, edge->wid);
    edge->wid = 0;
    edge->context = NULL;
    edge->grouped = false;
  }
}

static bool border_edge_active(struct border_edge* edge) {
  return edge->wid && !CGRectIsEmpty(edge->rect);
}

static CGPoint border_edge_origin(struct border* border, struct border_edge* edge) {
  return (CGPoint){ border->origin.x + edge->rect.origin.x,
                    border->origin.y + edge->rect.origin.y };
}

static void border_edge_rects(struct border* border, CGRect frame, struct settings* settings, CGRect rects[BORDER_EDGE_COUNT]) {
  // the target's edge sits this far inside the frame
  float edge = settings->border_width + BORDER_PADDING;
  bool glow = settings->active_window.stype == COLOR_STYLE_GLOW
              || settings->inactive_window.stype == COLOR_STYLE_GLOW;

  // a glow spreads into the padding, a plain line ends half its width outside the edge
  float outer = glow ? 0.f : fmaxf(floorf(edge - settings->border_width / 2.f) - 1.f, 0.f);
  // deepest the drawing reaches inside the edge, following the inner clip in border_draw_frame
  float inner = edge + 1.f;
  if (settings->border_style != BORDER_STYLE_SQUARE) {
    inner += 0.3f * border->inner_radius;
  } else if (settings->border_order == BORDER_ORDER_ABOVE
             && settings->border_width >= BORDER_TSMW) {
    inner += BORDER_TSMN;
  }
  inner = ceilf(inner);

  CGRect ring = CGRectInset(frame, outer, outer);
  float width = ring.size.width;
  float height = ring.size.height;
  float depth = inner - outer;
  float half = ceilf(height / 2.f);
  // the background fills the whole inside, so the top and bottom edges split it
  if (depth > half || (settings->show_background && settings->border_order != 1)) depth = half;

  float side_height = fmaxf(height - 2.f * depth, 0.f);
  rects[0] = CGRectMake(outer, outer, width, depth);
  rects[1] = CGRectMake(outer, outer + height - depth, width, depth);
  rects[2] = CGRectMake(outer, outer + depth, depth, side_height);
  rects[3] = CGRectMake(outer + width - depth, outer + depth, depth, side_height);
}

static bool border_check_too_small(struct border* border, CGRect window_frame) {
  CGRect smallest_rect = CGRectInset(window_frame, 1.0, 1.0);
  if (smallest_rect.size.width < 2.f * border->inner_radius
      || smallest_rect.size.height < 2.f * border->inner_radius) {
    return true;
  }
  return false;
}

static bool border_calculate_bounds(struct border* border, CGRect* frame, struct settings* settings) {
  CGRect window_frame;
  if (border->is_proxy) window_frame = border->target_bounds;
  else SLSGetWindowBounds(border->cid, border->target_wid, &window_frame);

  border->target_bounds = window_frame;
  border->too_small = border_check_too_small(border, window_frame);
  if (border->too_small) {
    border_hide(border);
    return false;
  }

  float border_offset = - settings->border_width - BORDER_PADDING;
  *frame = CGRectInset(window_frame, border_offset, border_offset);

  border->origin = frame->origin;
  frame->origin = CGPointZero;


  window_frame.origin = (CGPoint){ -border_offset, -border_offset };
  border->drawing_bounds = window_frame;

  return true;
}

static void border_draw_frame(struct border* border, CGContextRef context, CGRect frame, struct settings* settings) {
  CGContextSaveGState(context);
  struct color_style color_style = border->focused
                                   ? settings->active_window
                                   : settings->inactive_window;

  CGGradientRef gradient = NULL;
  CGPoint gradient_dir[2];
  if (color_style.stype == COLOR_STYLE_SOLID
     || color_style.stype == COLOR_STYLE_GLOW) {
    bool glow = color_style.stype == COLOR_STYLE_GLOW;
    drawing_set_stroke_and_fill(context, color_style.color, glow);
  } else if (color_style.stype == COLOR_STYLE_GRADIENT) {
    CGAffineTransform trans = CGAffineTransformMakeScale(frame.size.width,
                                                         frame.size.height);
    gradient = drawing_create_gradient(&color_style.gradient,
                                       trans,
                                       gradient_dir          );
  }

  CGContextSetLineWidth(context, settings->border_width);
  CGContextClearRect(context, frame);

  CGRect path_rect = border->drawing_bounds;
  CGMutablePathRef inner_clip_path = CGPathCreateMutable();
  if (settings->border_style == BORDER_STYLE_SQUARE
      && settings->border_order == BORDER_ORDER_ABOVE
      && settings->border_width >= BORDER_TSMW) {
    // Inset the frame to overlap the rounding of macOS windows to create a
    // truly square border
    path_rect = CGRectInset(border->drawing_bounds,
                            BORDER_TSMN,
                            BORDER_TSMN            );

    CGPathAddRect(inner_clip_path, NULL, path_rect);
  } else if (settings->border_style == BORDER_STYLE_SQUARE) {
    // a rounded clip would leave a rounded notch inside the square corners
    CGPathAddRect(inner_clip_path, NULL, CGRectInset(path_rect, 1.0, 1.0));
  } else {
    CGPathAddRoundedRect(inner_clip_path,
                         NULL,
                         CGRectInset(path_rect, 1.0, 1.0),
                         border->inner_radius,
                         border->inner_radius             );
  }
  drawing_clip_between_rect_and_path(context, frame, inner_clip_path);

  if (settings->border_style == BORDER_STYLE_SQUARE) {
    if (color_style.stype == COLOR_STYLE_SOLID
       || color_style.stype == COLOR_STYLE_GLOW) {
      drawing_draw_square_with_inset(context,
                                     path_rect,
                                     -settings->border_width / 2.f);
    }
    else if (color_style.stype == COLOR_STYLE_GRADIENT) {
      drawing_draw_square_gradient_with_inset(context,
                                              gradient,
                                              gradient_dir,
                                              path_rect,
                                              -settings->border_width / 2.f);
    }
  } else {
    float corner_radius = settings->border_style == BORDER_STYLE_ROUND_UNIFORM ? 9.0 : border->radius;

    if (settings->border_style == BORDER_STYLE_ROUND_UNIFORM) {
      drawing_draw_rounded_rect_with_inset(context,
                                           path_rect,
                                           corner_radius,
                                           true            );
    }

    if (color_style.stype == COLOR_STYLE_SOLID
       || color_style.stype == COLOR_STYLE_GLOW) {
      drawing_draw_rounded_rect_with_inset(context,
                                           path_rect,
                                           corner_radius,
                                           false           );
    } else if (color_style.stype == COLOR_STYLE_GRADIENT) {
      drawing_draw_rounded_gradient_with_inset(context,
                                               gradient,
                                               gradient_dir,
                                               path_rect,
                                               corner_radius  );
    }
  }
  CGGradientRelease(gradient);

  if (settings->show_background && settings->border_order != 1) {
    CGContextRestoreGState(context);
    CGContextSaveGState(context);
    color_style = settings->background;
    if (color_style.stype == COLOR_STYLE_SOLID
       || color_style.stype == COLOR_STYLE_GLOW) {
      drawing_draw_filled_path(context,
                               inner_clip_path,
                               color_style.color);
    }
  }
  CFRelease(inner_clip_path);
  CGContextRestoreGState(context);
}

static void border_draw(struct border* border, CGRect frame, struct settings* settings) {
  border->needs_redraw = false;
  for (int i = 0; i < BORDER_EDGE_COUNT; i++) {
    struct border_edge* edge = &border->edges[i];
    if (!border_edge_active(edge)) continue;

    CGContextSaveGState(edge->context);
    // window contexts have their origin at the bottom left
    CGContextTranslateCTM(edge->context,
                          -edge->rect.origin.x,
                          -(frame.size.height - CGRectGetMaxY(edge->rect)));
    border_draw_frame(border, edge->context, frame, settings);
    CGContextFlush(edge->context);
    CGContextRestoreGState(edge->context);
    SLSFlushWindowContentRegion(border->cid, edge->wid, NULL);
    SLSWindowThaw(border->cid, edge->wid);
  }
}

static void border_create_edge(struct border* border, struct border_edge* edge, CGRect rect, bool unmanaged, bool hidpi) {
  int cid = border->cid;
  CGRect window_frame = CGRectIsNull(rect)
                        ? CGRectNull
                        : (CGRect){ CGPointZero, rect.size };
  edge->wid = window_create(cid, window_frame, hidpi, unmanaged);
  edge->rect = rect;
  edge->context = SLWindowContextCreate(cid, edge->wid, NULL);
  CGContextSetInterpolationQuality(edge->context, kCGInterpolationNone);

  if (!border->sid) border->sid = window_space_id(cid, border->target_wid);
  window_send_to_space(cid, edge->wid, border->sid);
  border->needs_redraw = true;
}

void border_create_window(struct border* border, CGRect frame, bool unmanaged, bool hidpi) {
  pthread_mutex_lock(&border->mutex);
  for (int i = 0; i < BORDER_EDGE_COUNT; i++) {
    if (!border->edges[i].wid) {
      border_create_edge(border, &border->edges[i], CGRectNull, unmanaged, hidpi);
    }
  }
  border->frame = frame;
  pthread_mutex_unlock(&border->mutex);
}

void border_update_internal(struct border* border, struct settings* settings) {
  if (border->external_proxy_wid) return;

  int cid = border->cid;
  CGRect frame;
  if (!border_calculate_bounds(border, &frame, settings)) return;

  uint64_t tags = window_tags(cid, border->target_wid);
  border->sticky = tags & WINDOW_TAG_STICKY;
  if (!border->sticky && !is_space_visible(cid, border->sid)) return;


  bool shown = false;
  SLSWindowIsOrderedIn(cid, border->target_wid, &shown);
  if (!shown && !border->is_proxy) {
    border_hide(border);
    return;
  } 

  // asked once per process, and again only after a group request fails
  static int sa_supports_ordering_group = -1;
  if (sa_supports_ordering_group == -1) {
    sa_supports_ordering_group = yabai_sa_supports_ordering_group();
  }
  if (!border->edges[0].wid && !border->is_proxy) {
    border->use_ordering_group = sa_supports_ordering_group;
  }

  // grouped edges stay at the target's level, so windows in front of the target cover them
  // otherwise one level above the target, so raising the target can never cover the border
  int level = window_level(cid, border->target_wid)
              + (border->use_ordering_group ? 0 : 1);
  int sub_level = 0;

  CGRect rects[BORDER_EDGE_COUNT];
  border_edge_rects(border, frame, settings, rects);

  bool disabled_update = false;
  for (int i = 0; i < BORDER_EDGE_COUNT; i++) {
    struct border_edge* edge = &border->edges[i];
    if (CGRectIsEmpty(rects[i])) {
      if (edge->wid) {
        CFTypeRef transaction = SLSTransactionCreate(cid);
        if (transaction) {
          SLSTransactionOrderWindow(transaction, edge->wid, 0, border->target_wid);
          SLSTransactionCommit(transaction, 0);
          CFRelease(transaction);
        }
      }
      edge->rect = rects[i];
      continue;
    }

    if (!edge->wid) {
      border_create_edge(border, edge, rects[i], border->is_proxy, settings->hidpi);
    } else if (!CGRectEqualToRect(rects[i], edge->rect)) {
      if (!disabled_update) {
        disabled_update = true;
        SLSDisableUpdate(cid);
      }

      edge->rect = rects[i];
      CGRect shape = { CGPointZero, rects[i].size };
      CGPoint origin = border_edge_origin(border, edge);
      CFTypeRef shape_region;
      CGSNewRegionWithRect(&shape, &shape_region);

      SLSWindowFreezeWithOptions(cid, edge->wid, NULL);
      SLSSetWindowShape(cid, edge->wid, origin.x, origin.y, shape_region);
      CFRelease(shape_region);
      border->needs_redraw = true;
    }
  }

  if (!CGRectEqualToRect(frame, border->frame)) {
    border->needs_redraw = true;
    border->frame = frame;
  }

  if (border->needs_redraw) border_draw(border, frame, settings);

  CFTypeRef transaction = SLSTransactionCreate(cid);
  if(!transaction) return;
  for (int i = 0; i < BORDER_EDGE_COUNT; i++) {
    struct border_edge* edge = &border->edges[i];
    if (!border_edge_active(edge)) continue;

    CGPoint origin = border_edge_origin(border, edge);
    SLSTransactionMoveWindowWithGroup(transaction, edge->wid, origin);

    if (!border->is_proxy) {
      CGAffineTransform transform = CGAffineTransformIdentity;
      transform.tx = -origin.x;
      transform.ty = -origin.y;
      SLSTransactionSetWindowTransform(transaction,
                                       edge->wid,
                                       0,
                                       0,
                                       transform   );
    }
    SLSTransactionSetWindowLevel(transaction, edge->wid, level);
    SLSTransactionSetWindowSubLevel(transaction, edge->wid, sub_level);
    SLSTransactionOrderWindow(transaction,
                              edge->wid,
                              settings->border_order,
                              border->target_wid      );
  }
  SLSTransactionCommit(transaction, 0);
  CFRelease(transaction);

  if (border->use_ordering_group) {
    for (int i = 0; i < BORDER_EDGE_COUNT; i++) {
      struct border_edge* edge = &border->edges[i];
      if (!border_edge_active(edge) || edge->grouped) continue;
      edge->grouped = yabai_sa_ordering_group(border->target_wid, edge->wid, 1);
      // the scripting addition went away, so fall back to level + 1 on the next update
      if (!edge->grouped) {
        border->use_ordering_group = false;
        sa_supports_ordering_group = -1;
      }
    }
  }

  uint64_t set_tags = (1ULL << 1) | (1ULL << 9);
  uint64_t clear_tags = 0;

  if (border->sticky) {
    set_tags |= WINDOW_TAG_STICKY;
    clear_tags |= (1ULL << 45);
  }

  for (int i = 0; i < BORDER_EDGE_COUNT; i++) {
    struct border_edge* edge = &border->edges[i];
    if (!border_edge_active(edge)) continue;
    SLSSetWindowTags(cid, edge->wid, &set_tags, 0x40);
    SLSClearWindowTags(cid, edge->wid, &clear_tags, 0x40);
  }

  if (disabled_update) SLSReenableUpdate(cid);
}

static void* border_update_async_proc(void* context) {
  struct {
    struct border* border;
    struct settings settings;
  }* payload = context;

  pthread_mutex_lock(&payload->border->mutex);
  border_update_internal(payload->border, &payload->settings);
  pthread_mutex_unlock(&payload->border->mutex);
  free(payload);
  return NULL;
}

void border_init(struct border* border, int cid) {
  memset(border, 0, sizeof(struct border));
  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&border->mutex, &mattr);
  animation_init(&border->animation);
  if (cid) border->cid = cid;
  else border->cid = SLSMainConnectionID();
}

struct border* border_create() {
  struct border* border = malloc(sizeof(struct border));
  int cid = 0;
  SLSNewConnection(0, &cid);
  border_init(border, cid);
  return border;
}

void border_destroy(struct border* border) {
  border_hide(border);
  dispatch_async(dispatch_get_main_queue(), ^{
    pthread_mutex_lock(&border->mutex);
    border_destroy_window(border);
    if (border->proxy) border_destroy(border->proxy);
    animation_stop(&border->animation);
    if (!border->is_proxy && border->cid != SLSMainConnectionID())
      SLSReleaseConnection(border->cid);
    pthread_mutex_unlock(&border->mutex);
    free(border);
  });
}

void border_move(struct border* border) {
  pthread_mutex_lock(&border->mutex);
  if (border->external_proxy_wid) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }
  pthread_mutex_unlock(&border->mutex);

  struct settings* settings = border_get_settings(border);
  dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
    pthread_mutex_lock(&border->mutex);
    CGRect window_frame;
    SLSGetWindowBounds(border->cid, border->target_wid, &window_frame);
    CGPoint origin = { .x = window_frame.origin.x
                            - settings->border_width
                            - BORDER_PADDING,
                       .y = window_frame.origin.y
                            - settings->border_width
                            - BORDER_PADDING          };

    border->target_bounds = window_frame;
    border->origin = origin;
    CFTypeRef transaction = SLSTransactionCreate(border->cid);
    if (transaction) {
      for (int i = 0; i < BORDER_EDGE_COUNT; i++) {
        struct border_edge* edge = &border->edges[i];
        if (!border_edge_active(edge)) continue;
        SLSTransactionMoveWindowWithGroup(transaction,
                                          edge->wid,
                                          border_edge_origin(border, edge));
      }
      SLSTransactionCommit(transaction, 0);
      CFRelease(transaction);
    }
    pthread_mutex_unlock(&border->mutex);
  });
}

void border_update(struct border* border, bool try_async) {
  pthread_mutex_lock(&border->mutex);
  struct settings* settings = border_get_settings(border);
  border_update_internal(border, settings);
  pthread_mutex_unlock(&border->mutex);
  return;

  if (!border->edges[0].wid || !try_async) {
    border_update_internal(border, settings);
    pthread_mutex_unlock(&border->mutex);
    return;
  }

  struct payload {
    struct border* border;
    struct settings settings;
  }* payload = malloc(sizeof(struct payload));

  payload->border = border;
  payload->settings = *settings;

  pthread_t thread;
  pthread_create(&thread, NULL, border_update_async_proc, payload);
  pthread_detach(thread);
  pthread_mutex_unlock(&border->mutex);
}

void border_hide(struct border* border) {
  pthread_mutex_lock(&border->mutex);
  CFTypeRef transaction = SLSTransactionCreate(border->cid);
  if (transaction) {
    for (int i = 0; i < BORDER_EDGE_COUNT; i++) {
      if (!border->edges[i].wid) continue;
      SLSTransactionOrderWindow(transaction,
                                border->edges[i].wid,
                                0,
                                border->target_wid  );
    }
    SLSTransactionCommit(transaction, 0);
    CFRelease(transaction);
  }
  pthread_mutex_unlock(&border->mutex);
}

void border_unhide(struct border* border) {
  pthread_mutex_lock(&border->mutex);
  if (border->too_small
      || border->external_proxy_wid
      || (!border->sticky && !is_space_visible(border->cid, border->sid))) {
    pthread_mutex_unlock(&border->mutex);
    return;
  }

  struct settings* settings = border_get_settings(border);
  CFTypeRef transaction = SLSTransactionCreate(border->cid);
  if (transaction) {
    for (int i = 0; i < BORDER_EDGE_COUNT; i++) {
      if (!border_edge_active(&border->edges[i])) continue;
      SLSTransactionOrderWindow(transaction,
                                border->edges[i].wid,
                                settings->border_order,
                                border->target_wid      );
    }
    SLSTransactionCommit(transaction, 0);
    CFRelease(transaction);
  }
  pthread_mutex_unlock(&border->mutex);
}
