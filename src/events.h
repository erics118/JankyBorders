#pragma once

#define EVENT_WINDOW_UPDATE  723

#define EVENT_WINDOW_CLOSE   804
#define EVENT_WINDOW_MOVE    806
#define EVENT_WINDOW_RESIZE  807
#define EVENT_WINDOW_REORDER 808
#define EVENT_WINDOW_LEVEL   811
#define EVENT_WINDOW_UNHIDE  815
#define EVENT_WINDOW_HIDE    816

#define EVENT_WINDOW_TITLE   1322
#define EVENT_WINDOW_CREATE  1325
#define EVENT_WINDOW_DESTROY 1326
// the dock creates a transient space for each minimize or deminimize animation
#define EVENT_SPACE_CREATE   1327
#define EVENT_SPACE_DESTROY  1328

#define EVENT_SPACE_CHANGE   1401

#define EVENT_FRONT_CHANGE   1508

void events_register(int cid);
