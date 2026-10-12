#pragma once
#include <pthread.h>
#include "misc/helpers.h"
#include "misc/window.h"
#include "misc/drawing.h"
#include "animation.h"
#include "hashtable.h"

#define BORDER_ORDER_ABOVE 1
#define BORDER_ORDER_BELOW -1
#define BORDER_STYLE_ROUND  'r'
#define BORDER_STYLE_ROUND_UNIFORM 'u'
#define BORDER_STYLE_SQUARE 's'
#define BORDER_PADDING 8.0
#define BORDER_TSMN 3.27f

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000
#define BORDER_TSMW 52.f
#else
#define BORDER_TSMW 8.f
#endif

struct color_style {
  enum { COLOR_STYLE_GRADIENT, COLOR_STYLE_SOLID, COLOR_STYLE_GLOW } stype;
  union {
    uint32_t color;
    struct gradient gradient;
  };
};

struct settings {
  bool enabled;
  uint32_t apply_to;

  struct color_style active_window;
  struct color_style inactive_window;
  struct color_style corner_mask;
  struct color_style background;

  float border_width;
  float blur_radius;
  char border_style;
  bool hidpi;
  bool show_background;
  int border_order;
  bool ax_focus;

  bool blacklist_enabled;
  struct table blacklist;

  bool whitelist_enabled;
  struct table whitelist;
};

struct event_buffer {
  bool disable_coalescing;
  volatile bool is_coalescing;
  int64_t last_coalesce_attempt;
};

// one window per side, so no border window covers the inside of its target
// window pickers in screenshot tools then select the target, not the border
#define BORDER_EDGE_COUNT 4

struct border_edge {
  uint32_t wid;
  CGContextRef context;
  // in border frame coordinates, origin top left
  CGRect rect;
  bool grouped;
};

struct border {
  pthread_mutex_t mutex;
  int cid;

  bool focused;
  bool needs_redraw;
  bool too_small;
  bool sticky;
  // edges ride in the target's ordering group, so a raise moves them along with no flash
  bool use_ordering_group;

  uint64_t sid;
  struct border_edge edges[BORDER_EDGE_COUNT];
  uint32_t target_wid;

  float radius;
  float inner_radius;

  CGPoint origin;
  CGRect frame;
  CGRect target_bounds;
  CGRect drawing_bounds;

  struct animation animation;
  struct event_buffer event_buffer;

  bool is_proxy;
  struct border* proxy;
  volatile uint32_t external_proxy_wid;

  struct settings setting_override;
};

struct border* border_create();
void border_destroy(struct border* border);

void border_move(struct border* border);
void border_update(struct border* border, bool try_async);
void border_hide(struct border* border);
void border_unhide(struct border* border);

struct settings* border_get_settings(struct border* border);
