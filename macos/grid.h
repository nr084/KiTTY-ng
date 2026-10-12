#ifndef KITTY_NG_MAC_GRID_H
#define KITTY_NG_MAC_GRID_H
/* PuTTY draw runs carry their cell allocation in attributes, not locale data. */
static inline int kr_mac_run_cells(int length,unsigned long attr){return (attr&TATTR_COMBINING?1:length)*(attr&ATTR_WIDE?2:1);}
static inline int kr_mac_char_width(unsigned scalar){int width=mk_wcwidth(scalar);return width<0?1:width;}
static inline int kr_mac_line_width(unsigned long attr){return (attr&LATTR_MODE)==LATTR_NORM?1:2;}
static inline int kr_mac_line_height(unsigned long attr){unsigned mode=attr&LATTR_MODE;return mode==LATTR_TOP||mode==LATTR_BOT?2:1;}
static inline int kr_mac_line_offset(unsigned long attr,int height){return (attr&LATTR_MODE)==LATTR_BOT?-height:0;}
#endif
