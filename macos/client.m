/* Native AppKit adapter for the actual PuTTY terminal and Unix SSH backends. */
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#include "putty.h"
#include "ssh.h"
#include "terminal.h"
#include "storage.h"
#include "portable.h"
#include "client.h"
#include "grid.h"
#include "shortcuts.h"
#include "transfer.h"
#include "box-drawing.h"
#include "automation.h"
#include "language.h"
#include "utilities.h"
#include "editor.h"
#include "cli-language.h"
#include <sys/select.h>
#include <locale.h>
#include <wchar.h>
#include <limits.h>

static const CGFloat terminalPadding = 12;
@class MacTerminal;
typedef struct MacSession {
    Seat seat; TermWin termwin; LogPolicy logpolicy;
    __unsafe_unretained MacTerminal *view;
    Conf *conf; Terminal *term; Backend *backend; Ldisc *ldisc; LogContext *logctx;
    struct unicode_data ucs; rgb palette[OSC4_NCOLOURS];
    bool painting, closed, password_tried, started, raw_mouse, input_locked, commands_started, manual_abort;
    char *commands;size_t command_index;uint64_t command_due,reconnect_due;
} MacSession;
static NSMutableArray<MacTerminal *> *windows;
static bool pumping, initialized;
const unsigned cmdline_tooltype=TOOLTYPE_HOST_ARG|TOOLTYPE_PORT_ARG;
const bool use_pty_argv=false;
const bool share_can_be_downstream=true, share_can_be_upstream=true;
char **pty_argv;
char *pty_osx_envrestore_prefix;
static NSString *S(const char *text){if(!text)return @"";NSString *s=[NSString stringWithUTF8String:text];return s?:[[NSString alloc]initWithBytes:text length:strlen(text) encoding:NSWindowsCP1251StringEncoding]?:@"";}
static BOOL RU(void){return kr_mac_language()==1;}
static NSString *T(NSString *en,NSString *ru){return RU()?ru:en;}
static void message(const char *text){NSAlert *alert=[NSAlert new];alert.messageText=@"KiTTY-ng";alert.informativeText=S(kr_mac_translate(text));[alert runModal];}
void nonfatal(const char *format,...){va_list ap;va_start(ap,format);char *text=dupvprintf(format,ap);va_end(ap);message(text);sfree(text);}
void modalfatalbox(const char *format,...){va_list ap;va_start(ap,format);char *text=dupvprintf(format,ap);va_end(ap);message(text);sfree(text);[NSApp terminate:nil];exit(1);}
void cmdline_error(const char *format,...){va_list ap;va_start(ap,format);char *text=dupvprintf(format,ap);va_end(ap);message(text);sfree(text);}
void cleanup_exit(int code){exit(code);}
void old_keyfile_warning(void){message("The private key uses an old PPK format. KiTTYgen can save it in the current format.");}
char *platform_get_x_display(void){const char *p=getenv("DISPLAY");return p?dupstr(p):NULL;}
char *x_get_default(const char *key){return NULL;}
char *platform_default_s(const char *key){if(!strcmp(key,"SerialLine"))return dupstr("/dev/cu.usbserial");return NULL;}
bool platform_default_b(const char *key,bool value){return value;}
int platform_default_i(const char *key,int value){return !strcmp(key,"ShiftedArrowKeys")?1:value;}
FontSpec *platform_default_fontspec(const char *key){return fontspec_new(!strcmp(key,"Font")?"Menlo-Regular 13":"");}
Filename *platform_default_filename(const char *key){return filename_from_str(!strcmp(key,"LogFileName")?"kitty-ng.log":"");}
void timer_change_notify(unsigned long next){}
uxsel_id *uxsel_input_add(int fd,int rwx){return NULL;}
void uxsel_input_remove(uxsel_id *id){}

@interface MacTerminal : NSView <NSTextInputClient,NSWindowDelegate>
@property(assign) MacSession *session;
@property(nonatomic,strong) NSWindow *terminalWindow;
@property(nonatomic,strong) NSFont *font;
@property(nonatomic,strong) NSFont *boldFont;
@property(nonatomic,strong) NSFont *wideFont;
@property(nonatomic,strong) NSFont *wideBoldFont;
@property CGFloat cellWidth,cellHeight;
@property(nonatomic,strong) NSMutableAttributedString *marked;
@property(nonatomic,strong) NSMutableArray<NSString *> *events;
@property(nonatomic,strong) NSData *profileSource;
@property(nonatomic,strong) NSMutableData *oscData;
@property(nonatomic,strong) NSMutableArray *bindings;
@property(nonatomic,strong) NSString *remoteDirectory;
- (void)sendText:(NSString *)text;
- (void)start;
- (void)upload:(id)sender;
- (void)download:(id)sender;
- (void)service;
- (void)connect;
- (void)restart:(id)sender;
- (void)startup:(id)sender;
- (void)script:(id)sender;
- (void)woke:(NSNotification *)note;
- (void)observeOutput:(NSData *)data;
- (void)saveWindow;
- (void)toggleTop:(id)sender;
- (void)toggleLock:(id)sender;
- (void)hideWindow:(id)sender;
- (void)fullscreen:(id)sender;
- (void)showLog:(id)sender;
- (void)settings:(id)sender;
- (void)paste:(id)sender;
- (void)copy:(id)sender;
@end
/* Keep margins outside the terminal view: drawing, selection, IME and mouse
 * reporting all retain the same local cell coordinates. */
@interface MacTerminalFrame : NSView
@end
@implementation MacTerminalFrame
- (BOOL)isFlipped{return YES;}
- (void)drawRect:(NSRect)rect {
    [NSColor.windowBackgroundColor setFill];NSRectFill(rect);
    [[NSColor.separatorColor colorWithAlphaComponent:0.55] setStroke];
    NSBezierPath *border=[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds,5.5,5.5) xRadius:8 yRadius:8];
    border.lineWidth=1;[border stroke];
}
@end
static NSSize terminalContentSize(MacTerminal *view,int cols,int rows){return NSMakeSize(cols*view.cellWidth+2*terminalPadding,rows*view.cellHeight+2*terminalPadding);}
static MacSession *TW(TermWin *w){return container_of(w,MacSession,termwin);}
static MacSession *SEAT(Seat *w){return container_of(w,MacSession,seat);}
static NSColor *colour(MacSession *s,unsigned index,optionalrgb actual){rgb c=actual.enabled?(rgb){actual.r,actual.g,actual.b}:s->palette[index<OSC4_NCOLOURS?index:OSC4_COLOUR_fg];return [NSColor colorWithSRGBRed:c.r/255.0 green:c.g/255.0 blue:c.b/255.0 alpha:1];}
static bool draw_setup(TermWin *w){if(!TW(w)->painting)[TW(w)->view setNeedsDisplay:YES];return TW(w)->painting;}
static unsigned scalar(MacSession *s,unsigned c){
    switch(c&CSET_MASK){case CSET_ASCII:c=s->ucs.unitab_line[c&255];break;case CSET_LINEDRW:c=s->ucs.unitab_xterm[c&255];break;case CSET_SCOACS:c=s->ucs.unitab_scoacs[c&255];break;case CSET_GBCHR:c=(c&255)=='#'?0xa3:c&255;break;}
    return c>0x10ffff||(c>=0xd800&&c<=0xdfff)?0xfffd:c;
}
static NSString *run_string(MacSession *s,const wchar_t *text,int len){
    NSMutableString *result=[NSMutableString new];
    for(int i=0;i<len;i++){UTF32Char c=scalar(s,text[i]);NSString *part=[[NSString alloc]initWithBytes:&c length:sizeof(c) encoding:NSUTF32LittleEndianStringEncoding];if(part)[result appendString:part];}
    return result;
}
static NSFont *configuredFont(const char *spec,CGFloat fallback){NSString *value=S(spec);NSRange split=[value rangeOfString:@" " options:NSBackwardsSearch];CGFloat size=split.location==NSNotFound?fallback:[[value substringFromIndex:split.location+1]doubleValue];NSString *face=split.location==NSNotFound?value:[value substringToIndex:split.location];if(size<6||size>72)size=fallback;return [NSFont fontWithName:face size:size];}
static bool draw_box_glyph(NSString *glyph,NSRect cell,NSColor *foreground,CGFloat scale){
    if(glyph.length!=1)return false;unichar ch=[glyph characterAtIndex:0];KrMacLine lines[8];int count=kr_mac_box_lines(ch,cell.size.width,cell.size.height,MAX(1,scale),lines);if(!count)return false;
    [foreground setStroke];
    CGContextSetShouldAntialias(NSGraphicsContext.currentContext.CGContext,false);
    for(int i=0;i<count;i++){KrMacLine line=lines[i];NSBezierPath *path=[NSBezierPath bezierPath];path.lineWidth=line.width;path.lineCapStyle=NSLineCapStyleButt;
        unsigned dash=kr_mac_boxes[ch-0x2500].dashes;if(dash){CGFloat unit=(line.x1==line.x2?cell.size.height:cell.size.width)/(2*dash);CGFloat pattern[]={unit,unit};[path setLineDash:pattern count:2 phase:0];}
        [path moveToPoint:NSMakePoint(cell.origin.x+line.x1,cell.origin.y+line.y1)];[path lineToPoint:NSMakePoint(cell.origin.x+line.x2,cell.origin.y+line.y2)];[path stroke];}
    return true;
}
static void draw_text(TermWin *w,int x,int y,wchar_t *text,int len,unsigned long attr,int lattr,truecolour tc){
    if(len<=0)return;MacSession *s=TW(w);MacTerminal *v=s->view;
    unsigned fg=(attr&ATTR_FGMASK)>>ATTR_FGSHIFT,bg=(attr&ATTR_BGMASK)>>ATTR_BGSHIFT;
    if(attr&ATTR_REVERSE){unsigned swap=fg;fg=bg;bg=swap;optionalrgb swaprgb=tc.fg;tc.fg=tc.bg;tc.bg=swaprgb;}
    if((conf_get_int(s->conf,CONF_bold_style)&BOLD_STYLE_COLOUR)&&(attr&ATTR_BOLD)){if(fg<16)fg|=8;else if(fg>=256)fg|=1;}
    if(attr&ATTR_ACTCURS){fg=OSC4_COLOUR_cursor_fg;bg=OSC4_COLOUR_cursor_bg;tc.fg=tc.bg=optionalrgb_none;}
    CGFloat factor=kr_mac_line_width(lattr),step=v.cellWidth*(attr&ATTR_WIDE?2:1)*factor;
    NSRect box=NSMakeRect(x*v.cellWidth*factor,y*v.cellHeight,kr_mac_run_cells(len,attr)*v.cellWidth*factor,v.cellHeight);
    [colour(s,bg,tc.bg)setFill];NSRectFill(box);
    bool bold=(attr&ATTR_BOLD)&&(conf_get_int(s->conf,CONF_bold_style)&BOLD_STYLE_FONT),wide=(attr&ATTR_WIDE)!=0;NSFont *font=wide?(bold?v.wideBoldFont:v.wideFont):(bold?v.boldFont:v.font);
    NSMutableDictionary *attrs=[@{NSFontAttributeName:font,NSForegroundColorAttributeName:colour(s,fg,tc.fg),NSLigatureAttributeName:@0}mutableCopy];
    if(attr&ATTR_UNDER)attrs[NSUnderlineStyleAttributeName]=@(NSUnderlineStyleSingle);
    if(attr&ATTR_STRIKE)attrs[NSStrikethroughStyleAttributeName]=@(NSUnderlineStyleSingle);
    /* No natural string advance: each base glyph starts on the terminal grid.
     * Combining runs form one cluster, occupying the allocation from PuTTY. */
    int clusters=(attr&TATTR_COMBINING)?1:len;
    for(int i=0;i<clusters;i++){
        NSString *glyph=run_string(s,text+(clusters==1?0:i),clusters==1?len:1);
        NSRect cell=NSMakeRect(box.origin.x+i*step,box.origin.y,step,v.cellHeight);
        [NSGraphicsContext saveGraphicsState];NSRectClip(cell);
        if((lattr&LATTR_MODE)==LATTR_NORM&&draw_box_glyph(glyph,cell,colour(s,fg,tc.fg),v.window.backingScaleFactor)){[NSGraphicsContext restoreGraphicsState];continue;}
        CGFloat vertical=kr_mac_line_height(lattr);NSAffineTransform *transform=[NSAffineTransform transform];[transform translateXBy:cell.origin.x yBy:cell.origin.y+kr_mac_line_offset(lattr,v.cellHeight)];[transform scaleXBy:factor yBy:vertical];[transform concat];[glyph drawAtPoint:NSMakePoint(0,1) withAttributes:attrs];
        [NSGraphicsContext restoreGraphicsState];
    }
}
static void draw_cursor(TermWin *w,int x,int y,wchar_t *text,int len,unsigned long attr,int lattr,truecolour tc){
    if(!(attr&(ATTR_ACTCURS|ATTR_PASCURS)))return;MacSession *s=TW(w);MacTerminal *v=s->view;
    [colour(s,OSC4_COLOUR_cursor_bg,optionalrgb_none)setStroke];NSFrameRectWithWidth(NSMakeRect(x*v.cellWidth*kr_mac_line_width(lattr),y*v.cellHeight,v.cellWidth*kr_mac_line_width(lattr)*(attr&ATTR_WIDE?2:1),v.cellHeight),1.5);
}
static void draw_sigil(TermWin *w,int x,int y){MacTerminal *v=TW(w)->view;[[NSColor systemBlueColor] setFill];NSRectFill(NSMakeRect(x*v.cellWidth,y*v.cellHeight,3,v.cellHeight));}
static int char_width(TermWin *w,int c){return kr_mac_char_width(c);}
static void draw_free(TermWin *w){}
static void cursor_pos(TermWin *w,int x,int y){}
static void mouse_mode(TermWin *w,bool enabled){TW(w)->raw_mouse=enabled;}
static void mouse_pointer(TermWin *w,bool enabled){}
static void scrollbar(TermWin *w,int total,int start,int page){}
static void bell(TermWin *w,int mode){NSBeep();if(conf_get_bool(TW(w)->conf,CONF_foreground_on_bell))[TW(w)->view.terminalWindow makeKeyAndOrderFront:nil];}
static void clip_write(TermWin *w,int clipboard,wchar_t *text,int *attrs,truecolour *colours,int len,bool deselect){NSString *s=[[NSString alloc]initWithBytes:text length:len*sizeof(wchar_t) encoding:NSUTF32LittleEndianStringEncoding];if(s){[NSPasteboard.generalPasteboard clearContents];[NSPasteboard.generalPasteboard setString:s forType:NSPasteboardTypeString];}}
static void clip_paste(TermWin *w,int clipboard){[TW(w)->view paste:nil];}
static void remote_clipboard(TermWin *w,const char *encoded){
    MacSession *s=TW(w);if(!s->started||!conf_get_bool(s->conf,CONF_kr_remote_clipboard))return;const char *semi=strchr(encoded,';');if(!semi||(semi!=encoded&&(semi!=encoded+1||encoded[0]!='c')))return;
    const char *value=semi+1;size_t n=strlen(value);if(!n||n>65532||n%4||!base64_valid(ptrlen_from_asciz(value)))return;
    NSData *data=[[NSData alloc]initWithBase64EncodedString:S(value) options:0];if(!data||memchr(data.bytes,0,data.length))return;
    NSString *text=[[NSString alloc]initWithData:data encoding:NSUTF8StringEncoding];if(text){[NSPasteboard.generalPasteboard clearContents];[NSPasteboard.generalPasteboard setString:text forType:NSPasteboardTypeString];}
}
static void refresh(TermWin *w){[TW(w)->view setNeedsDisplay:YES];}
static void request_resize(TermWin *w,int cols,int rows){MacSession *s=TW(w);if(cols>0&&rows>0&&cols<=1000&&rows<=1000)[s->view.terminalWindow setContentSize:terminalContentSize(s->view,cols,rows)];term_resize_request_completed(s->term);}
static void title(TermWin *w,const char *text,int cp){TW(w)->view.terminalWindow.title=[@"KiTTY-ng — " stringByAppendingString:S(text)];}
static void icon_title(TermWin *w,const char *text,int cp){}
static void minimized(TermWin *w,bool state){if(state)[TW(w)->view.terminalWindow miniaturize:nil];else[TW(w)->view.terminalWindow deminiaturize:nil];}
static void maximized(TermWin *w,bool state){NSWindow *window=TW(w)->view.terminalWindow;if(window.zoomed!=state)[window zoom:nil];}
static void move_window(TermWin *w,int x,int y){[TW(w)->view.terminalWindow setFrameOrigin:NSMakePoint(x,y)];}
static void zorder(TermWin *w,bool top){TW(w)->view.terminalWindow.level=top?NSFloatingWindowLevel:NSNormalWindowLevel;}
static void palette_set(TermWin *w,unsigned start,unsigned count,const rgb *colours){MacSession *s=TW(w);if(start+count<=OSC4_NCOLOURS)memcpy(s->palette+start,colours,count*sizeof(rgb));[s->view setNeedsDisplay:YES];}
static void palette_override(TermWin *w,Terminal *term){}
static void unthrottle(TermWin *w,size_t backlog){MacSession *s=TW(w);if(s->backend)backend_unthrottle(s->backend,backlog);}
static const TermWinVtable term_vt={.setup_draw_ctx=draw_setup,.draw_text=draw_text,.draw_cursor=draw_cursor,.draw_trust_sigil=draw_sigil,.char_width=char_width,.free_draw_ctx=draw_free,.set_cursor_pos=cursor_pos,.set_raw_mouse_mode=mouse_mode,.set_raw_mouse_mode_pointer=mouse_pointer,.set_scrollbar=scrollbar,.bell=bell,.clip_write=clip_write,.clip_request_paste=clip_paste,.remote_clipboard=remote_clipboard,.refresh=refresh,.request_resize=request_resize,.set_title=title,.set_icon_title=icon_title,.set_minimised=minimized,.set_maximised=maximized,.move=move_window,.set_zorder=zorder,.palette_set=palette_set,.palette_get_overrides=palette_override,.unthrottle=unthrottle};
static size_t output(Seat *seat,SeatOutputType type,const void *data,size_t len){MacSession *s=SEAT(seat);if(s->term){[s->view observeOutput:[NSData dataWithBytes:data length:len]];term_data(s->term,data,len);[s->view setNeedsDisplay:YES];}return 0;}
static size_t banner(Seat *seat,const void *data,size_t len){return output(seat,SEAT_OUTPUT_STDERR,data,len);}
static bool eof(Seat *seat){return true;}
static void started(Seat *seat){SEAT(seat)->started=true;}
static uint64_t now(void){return NSProcessInfo.processInfo.systemUptime*1000;}
static void cancel_commands(MacSession *s){if(s->commands){smemclr(s->commands,strlen(s->commands));sfree(s->commands);s->commands=NULL;}s->command_index=0;}
static void exited(Seat *seat){MacSession *s=SEAT(seat);s->started=false;s->closed=true;cancel_commands(s);s->view.terminalWindow.title=[s->view.terminalWindow.title stringByAppendingString:T(@" — closed",@" — закрыто")];int policy=conf_get_int(s->conf,CONF_close_on_exit),code=s->backend?backend_exitcode(s->backend):-1;if(policy==FORCE_ON||(policy==AUTO&&code==0)){__weak MacTerminal *view=s->view;dispatch_async(dispatch_get_main_queue(),^{MacTerminal *owner=view;if(owner.session&&owner.session->closed)[owner.terminalWindow close];});}}
static void fatal(Seat *seat,const char *error){MacSession *s=SEAT(seat);s->started=false;s->closed=true;cancel_commands(s);int delay=conf_get_int(s->conf,CONF_kr_reconnect_delay);
    if(delay>0&&!s->manual_abort){s->reconnect_due=now()+(uint64_t)delay*1000;[s->view.events addObject:S(error)];s->view.terminalWindow.title=T(@"KiTTY-ng — reconnecting…",@"KiTTY-ng — переподключение…");}else message(error);
}
static void nonfatal_seat(Seat *seat,const char *error){message(error);}
static NSString *dialog_text(SeatDialogText *text){NSMutableString *s=[NSMutableString new];for(size_t i=0;i<text->nitems;i++)if(text->items[i].type!=SDT_BATCH_ABORT)[s appendFormat:@"%@\n\n",S(text->items[i].text)];return s;}
static SeatPromptResult host_key(Seat *seat,const char *host,int port,const char *type,char *key,SeatDialogText *text,HelpCtx help,void(*callback)(void*,SeatPromptResult),void *context){
    NSAlert *alert=[NSAlert new];alert.messageText=T(@"SSH server identity",@"Подлинность SSH-сервера");alert.informativeText=dialog_text(text);
    [alert addButtonWithTitle:T(@"Cancel",@"Отмена")];[alert addButtonWithTitle:T(@"Accept and save",@"Принять и сохранить")];[alert addButtonWithTitle:T(@"Connect once",@"Подключиться один раз")];NSInteger choice=[alert runModal];
    if(choice==NSAlertFirstButtonReturn)SEAT(seat)->manual_abort=true;
    if(choice==NSAlertSecondButtonReturn)store_host_key(seat,host,port,type,key);return choice==NSAlertFirstButtonReturn?SPR_USER_ABORT:SPR_OK;
}
static SeatPromptResult weak_crypto(Seat *seat,SeatDialogText *text,void(*callback)(void*,SeatPromptResult),void *context){NSAlert *a=[NSAlert new];a.messageText=T(@"SSH security warning",@"Предупреждение безопасности SSH");a.informativeText=dialog_text(text);[a addButtonWithTitle:T(@"Cancel",@"Отмена")];[a addButtonWithTitle:T(@"Continue",@"Продолжить")];BOOL accepted=[a runModal]==NSAlertSecondButtonReturn;if(!accepted)SEAT(seat)->manual_abort=true;return accepted?SPR_OK:SPR_USER_ABORT;}
static SeatPromptResult prompts(Seat *seat,prompts_t *p){MacSession *s=SEAT(seat);
    if(!s->password_tried&&p->n_prompts==1&&!p->prompts[0]->echo&&p->to_server&&(!p->from_server||strstr(p->prompts[0]->prompt,"assword"))){const char *password=conf_get_str(s->conf,CONF_password);if(*password){prompt_set_result(p->prompts[0],password);s->password_tried=true;return SPR_OK;}}
    for(size_t i=0;i<p->n_prompts;i++){NSAlert *a=[NSAlert new];a.messageText=@"KiTTY-ng — SSH";a.informativeText=S(p->from_server?p->prompts[i]->prompt:kr_mac_translate(p->prompts[i]->prompt));NSTextField *field=p->prompts[i]->echo?[[NSTextField alloc]initWithFrame:NSMakeRect(0,0,350,28)]:[[NSSecureTextField alloc]initWithFrame:NSMakeRect(0,0,350,28)];a.accessoryView=field;[a addButtonWithTitle:T(@"Continue",@"Продолжить")];[a addButtonWithTitle:T(@"Cancel",@"Отмена")];[a.window makeFirstResponder:field];if([a runModal]!=NSAlertFirstButtonReturn){s->manual_abort=true;return SPR_USER_ABORT;}prompt_set_result(p->prompts[i],field.stringValue.UTF8String);field.stringValue=@"";}return SPR_OK;
}
static void trust(Seat *seat,bool value){if(SEAT(seat)->term)term_set_trust_status(SEAT(seat)->term,value);}
static bool pixel_size(Seat *seat,int *w,int *h){NSSize z=SEAT(seat)->view.bounds.size;*w=z.width;*h=z.height;return true;}
static bool cursor(Seat *seat,int *x,int *y){if(!SEAT(seat)->term)return false;term_get_cursor_position(SEAT(seat)->term,x,y);return true;}
/* Notification from Ldisc, not a request to update it. Match the GTK seat. */
static const SeatDialogPromptDescriptions descriptions={"Accept and save","Connect once","Cancel","Cancelled","Continue","Cancel"};
static const SeatDialogPromptDescriptions *prompt_descriptions(Seat *seat){return &descriptions;}
static const SeatVtable seat_vt={.output=output,.eof=eof,.sent=nullseat_sent,.banner=banner,.get_userpass_input=prompts,.notify_session_started=started,.notify_remote_exit=exited,.notify_remote_disconnect=nullseat_notify_remote_disconnect,.connection_fatal=fatal,.nonfatal=nonfatal_seat,.update_specials_menu=nullseat_update_specials_menu,.get_ttymode=nullseat_get_ttymode,.set_busy_status=nullseat_set_busy_status,.confirm_ssh_host_key=host_key,.confirm_weak_crypto_primitive=weak_crypto,.confirm_weak_cached_hostkey=weak_crypto,.prompt_descriptions=prompt_descriptions,.is_utf8=nullseat_is_always_utf8,.echoedit_update=nullseat_echoedit_update,.get_display=nullseat_get_display,.get_windowid=nullseat_get_windowid,.get_window_pixel_size=pixel_size,.stripctrl_new=nullseat_stripctrl_new,.set_trust_status=trust,.can_set_trust_status=nullseat_can_set_trust_status_yes,.has_mixed_input_stream=nullseat_has_mixed_input_stream_no,.verbose=nullseat_verbose_yes,.interactive=nullseat_interactive_yes,.get_cursor_position=cursor};
static void log_event(LogPolicy *lp,const char *text){MacSession *s=container_of(lp,MacSession,logpolicy);[s->view.events addObject:S(text)];if(s->view.events.count>2000)[s->view.events removeObjectAtIndex:0];}
static int log_append(LogPolicy *lp,Filename *file,void(*callback)(void*,int),void *context){return 1;}
static void log_error(LogPolicy *lp,const char *text){message(text);}
static bool log_verbose(LogPolicy *lp){return true;}
static const LogPolicyVtable log_vt={.eventlog=log_event,.askappend=log_append,.logging_error=log_error,.verbose=log_verbose};

@implementation MacTerminal
- (NSMenu *)menuForEvent:(NSEvent *)event {
    NSMenu *menu=[NSMenu new];NSArray *items=@[@[T(@"Session settings…",@"Настройки сеанса…"),NSStringFromSelector(@selector(settings:))],@[T(@"Event log…",@"Журнал событий…"),NSStringFromSelector(@selector(showLog:))],@[T(@"Always on top",@"Поверх окон"),NSStringFromSelector(@selector(toggleTop:))],@[T(@"Protect input",@"Защита ввода"),NSStringFromSelector(@selector(toggleLock:))],@[T(@"Hide window",@"Скрыть окно"),NSStringFromSelector(@selector(hideWindow:))],@[T(@"Fullscreen",@"Полный экран"),NSStringFromSelector(@selector(fullscreen:))],@[T(@"Restart session",@"Перезапустить сеанс"),NSStringFromSelector(@selector(restart:))],@[T(@"Send automatic commands",@"Отправить автокоманды"),NSStringFromSelector(@selector(startup:))],@[T(@"Send script…",@"Отправить скрипт…"),NSStringFromSelector(@selector(script:))],@[T(@"Send files…",@"Отправить файлы…"),NSStringFromSelector(@selector(upload:))],@[T(@"Download files…",@"Скачать файлы…"),NSStringFromSelector(@selector(download:))],@[T(@"Copy",@"Копировать"),NSStringFromSelector(@selector(copy:))],@[T(@"Paste",@"Вставить"),NSStringFromSelector(@selector(paste:))]];
    for(NSArray *item in items){NSMenuItem *entry=[[NSMenuItem alloc]initWithTitle:item[0] action:NSSelectorFromString(item[1]) keyEquivalent:@""];entry.target=self;[menu addItem:entry];}return menu;
}
- (void)upload:(id)sender{if(self.session&&!self.session->input_locked)kr_mac_upload(self.session->conf,self.terminalWindow,nil,conf_get_bool(self.session->conf,CONF_scp_auto_pwd)?self.remoteDirectory:nil);}
- (void)download:(id)sender{if(self.session&&!self.session->input_locked)kr_mac_download(self.session->conf,self.terminalWindow,conf_get_bool(self.session->conf,CONF_scp_auto_pwd)?self.remoteDirectory:nil);}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender{return self.session&&!self.session->input_locked&&conf_get_int(self.session->conf,CONF_protocol)==PROT_SSH?NSDragOperationCopy:NSDragOperationNone;}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender{
    if(!self.session||self.session->input_locked)return NO;NSArray *files=[sender.draggingPasteboard readObjectsForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];if(!files.count)return NO;kr_mac_upload(self.session->conf,self.terminalWindow,files,conf_get_bool(self.session->conf,CONF_scp_auto_pwd)?self.remoteDirectory:nil);return YES;
}
- (void)observeOutput:(NSData *)data {
    if(!self.session||!self.session->started)return;if(!self.oscData)self.oscData=[NSMutableData new];[self.oscData appendData:data];
    if(self.oscData.length>65536)[self.oscData replaceBytesInRange:NSMakeRange(0,self.oscData.length-32768) withBytes:NULL length:0];
    const unsigned char *bytes=self.oscData.bytes;NSUInteger n=self.oscData.length,consumed=0;
    for(NSUInteger i=0;i+4<n;i++)if(bytes[i]==27&&bytes[i+1]==']'&&bytes[i+2]=='7'&&bytes[i+3]==';'){
        NSUInteger end=i+4;while(end<n&&bytes[end]!=7&&!(bytes[end]==27&&end+1<n&&bytes[end+1]=='\\'))end++;if(end==n)break;
        NSString *value=[[NSString alloc]initWithBytes:bytes+i+4 length:end-i-4 encoding:NSUTF8StringEncoding];NSURLComponents *url=value?[NSURLComponents componentsWithString:value]:nil;
        if([url.scheme.lowercaseString isEqualToString:@"file"]&&[url.path hasPrefix:@"/"]&&[url.path rangeOfCharacterFromSet:NSCharacterSet.newlineCharacterSet].location==NSNotFound)self.remoteDirectory=url.path;
        consumed=end+(bytes[end]==7?1:2);i=consumed-1;
    }
    if(consumed)[self.oscData replaceBytesInRange:NSMakeRange(0,consumed) withBytes:NULL length:0];
}
- (void)toggleTop:(id)sender {if(self.session){bool top=!conf_get_bool(self.session->conf,CONF_alwaysontop);conf_set_bool(self.session->conf,CONF_alwaysontop,top);self.terminalWindow.level=top?NSFloatingWindowLevel:NSNormalWindowLevel;}}
- (void)toggleLock:(id)sender {if(self.session)self.session->input_locked=!self.session->input_locked;}
- (void)hideWindow:(id)sender {[self.terminalWindow orderOut:nil];}
- (void)fullscreen:(id)sender {[self.terminalWindow toggleFullScreen:nil];}
- (void)settings:(id)sender {if(self.session)kr_mac_edit_profile(conf_get_str(self.session->conf,CONF_kr_session_name),true);}
- (void)showLog:(id)sender {NSAlert *alert=[NSAlert new];alert.messageText=T(@"Event log",@"Журнал событий");NSScrollView *scroll=[[NSScrollView alloc]initWithFrame:NSMakeRect(0,0,650,300)];scroll.hasVerticalScroller=YES;NSTextView *text=[[NSTextView alloc]initWithFrame:scroll.bounds];text.editable=NO;text.string=[self.events componentsJoinedByString:@"\n"];text.font=[NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];scroll.documentView=text;alert.accessoryView=scroll;[alert runModal];}
- (void)saveWindow {
    MacSession *s=self.session;if(!s)return;const char *name=conf_get_str(s->conf,CONF_kr_session_name);if(!*name)return;char *path=kr_mac_profile_path(name);NSData *current=path?[NSData dataWithContentsOfFile:S(path)]:nil;sfree(path);
    if(!current||![current isEqualToData:self.profileSource])return;
    if(conf_get_bool(s->conf,CONF_saveonexit)){conf_set_int(s->conf,CONF_width,s->term->cols);conf_set_int(s->conf,CONF_height,s->term->rows);char *failure=save_settings(name,s->conf);bool ok=!failure&&kr_mac_write_succeeded();sfree(failure);if(!ok)return;}
    if(conf_get_bool(s->conf,CONF_save_windowpos)){char *failure=NULL;settings_w *writer=open_settings_w(name,&failure);if(writer){NSRect frame=self.terminalWindow.frame;write_setting_i(writer,"MacWindowX",frame.origin.x);write_setting_i(writer,"MacWindowY",frame.origin.y);write_setting_i(writer,"MacWindowWidth",frame.size.width);write_setting_i(writer,"MacWindowHeight",frame.size.height);close_settings_w(writer);}sfree(failure);}
}
- (BOOL)isFlipped{return YES;}
- (BOOL)acceptsFirstResponder{return YES;}
- (void)start {
    MacSession *s=self.session;NSString *spec=S(conf_get_fontspec(s->conf,CONF_font)->name);NSRange space=[spec rangeOfString:@" " options:NSBackwardsSearch];CGFloat pointSize=13;NSString *face=spec;if(space.location!=NSNotFound){pointSize=[[spec substringFromIndex:space.location+1]doubleValue];face=[spec substringToIndex:space.location];}if(pointSize<6||pointSize>72)pointSize=13;self.font=[NSFont fontWithName:face size:pointSize]?:[NSFont monospacedSystemFontOfSize:pointSize weight:NSFontWeightRegular];self.boldFont=configuredFont(conf_get_fontspec(s->conf,CONF_boldfont)->name,pointSize)?:[NSFontManager.sharedFontManager convertFont:self.font toHaveTrait:NSBoldFontMask];self.wideFont=configuredFont(conf_get_fontspec(s->conf,CONF_widefont)->name,pointSize)?:self.font;self.wideBoldFont=configuredFont(conf_get_fontspec(s->conf,CONF_wideboldfont)->name,pointSize)?:self.boldFont;self.cellWidth=ceil([@"M" sizeWithAttributes:@{NSFontAttributeName:self.font}].width);self.cellHeight=ceil(self.font.ascender-self.font.descender+self.font.leading+2);self.events=[NSMutableArray new];self.marked=[NSMutableAttributedString new];
    [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    [self loadBindings];s->seat.vt=&seat_vt;s->termwin.vt=&term_vt;s->logpolicy.vt=&log_vt;s->view=self;
    init_ucs(&s->ucs,conf_get_str(s->conf,CONF_line_codepage),conf_get_bool(s->conf,CONF_utf8_override),CS_UTF8,conf_get_int(s->conf,CONF_vtmode));s->term=term_init(s->conf,&s->ucs,&s->termwin);s->logctx=log_init(&s->logpolicy,s->conf);term_provide_logctx(s->term,s->logctx);
    [self.terminalWindow setContentSize:terminalContentSize(self,MAX(20,conf_get_int(s->conf,CONF_width)),MAX(8,conf_get_int(s->conf,CONF_height)))];term_size(s->term,MAX(8,self.bounds.size.height/self.cellHeight),MAX(20,self.bounds.size.width/self.cellWidth),conf_get_int(s->conf,CONF_savelines));
    [NSWorkspace.sharedWorkspace.notificationCenter addObserver:self selector:@selector(woke:) name:NSWorkspaceDidWakeNotification object:nil];
    [self connect];
    if(conf_get_bool(s->conf,CONF_alwaysontop))self.terminalWindow.level=NSFloatingWindowLevel;
    int transparency=conf_get_int(s->conf,CONF_transparencynumber);if(transparency>=0)self.terminalWindow.alphaValue=MAX(0.1,1.0-MIN(transparency,255)/255.0);
    
}
- (void)connect {
    MacSession *s=self.session;if(!s)return;s->started=false;s->closed=false;s->manual_abort=false;s->password_tried=false;s->commands_started=false;s->reconnect_due=0;cancel_commands(s);
    if(s->ldisc){ldisc_free(s->ldisc);s->ldisc=NULL;}if(s->backend){backend_free(s->backend);s->backend=NULL;}term_provide_backend(s->term,NULL);
    char *realhost=NULL;const struct BackendVtable *backend=backend_vt_from_proto(conf_get_int(s->conf,CONF_protocol));char *error=backend?backend_init(backend,&s->seat,&s->backend,s->logctx,s->conf,conf_get_str(s->conf,CONF_host),conf_get_int(s->conf,CONF_port),&realhost,conf_get_bool(s->conf,CONF_tcp_nodelay),conf_get_bool(s->conf,CONF_tcp_keepalives)):dupstr("This protocol is unavailable on macOS");
    if(error){fatal(&s->seat,error);sfree(error);}else{term_provide_backend(s->term,s->backend);s->ldisc=ldisc_create(s->conf,s->term,s->backend,&s->seat);term_setup_window_titles(s->term,realhost);backend_size(s->backend,s->term->cols,s->term->rows);}sfree(realhost);[self setNeedsDisplay:YES];
}
- (void)restart:(id)sender{[self connect];}
- (void)woke:(NSNotification *)note{if(self.session&&conf_get_bool(self.session->conf,CONF_wakeup_reconnect))self.session->reconnect_due=now()+500;}
- (void)startup:(id)sender{if(self.session){cancel_commands(self.session);self.session->commands_started=false;}}
- (void)script:(id)sender{
    if(!self.session||self.session->input_locked)return;NSOpenPanel *panel=[NSOpenPanel openPanel];if([panel runModal]!=NSModalResponseOK||!self.session)return;
    Filename *file=filename_from_str(panel.URL.path.UTF8String);char *failure=NULL,*command=kr_mac_script_text(file,&failure);filename_free(file);
    if(failure){message(failure);sfree(failure);return;}cancel_commands(self.session);self.session->commands=command;self.session->commands_started=true;self.session->command_due=now();
}
- (void)service {
    MacSession *s=self.session;if(!s)return;
    if(s->reconnect_due&&now()>=s->reconnect_due){[self connect];return;}
    if(s->closed||!s->started||!s->backend||!backend_sendok(s->backend)||s->input_locked)return;
    if(!s->commands_started){s->commands_started=true;char *failure=NULL;s->commands=kr_mac_startup_commands(s->conf,&failure);s->command_due=now()+conf_get_int(s->conf,CONF_initdelay);if(failure){message(failure);sfree(failure);return;}}
    if(!s->commands||now()<s->command_due)return;const unsigned char *data=(const unsigned char*)s->commands+s->command_index;if(!*data){cancel_commands(s);return;}
    int delay=conf_get_int(s->conf,CONF_bcdelay);size_t n=1;
    if(!delay){while(data[n-1]&&data[n-1]!='\r')n++;if(!data[n-1])n--;}
    else if(data[0]>=0xc0){n=data[0]<0xe0?2:data[0]<0xf0?3:4;}
    if(n){term_keyinput(s->term,CS_UTF8,(const char*)data,n);s->command_index+=n;s->command_due=now()+(data[n-1]=='\r'?conf_get_int(s->conf,CONF_kr_command_delay):delay);}
}
- (void)setFrameSize:(NSSize)size{[super setFrameSize:size];if(self.session&&self.session->term&&self.cellWidth>0){int cols=MAX(1,size.width/self.cellWidth),rows=MAX(1,size.height/self.cellHeight);term_size(self.session->term,rows,cols,conf_get_int(self.session->conf,CONF_savelines));if(self.session->backend)backend_size(self.session->backend,cols,rows);[self setNeedsDisplay:YES];}}
- (void)drawRect:(NSRect)rect{[((self.session&&self.session->term)?colour(self.session,OSC4_COLOUR_bg,optionalrgb_none):NSColor.blackColor) setFill];NSRectFill(rect);if(self.session&&self.session->term){self.session->painting=true;term_paint(self.session->term,0,0,self.session->term->cols-1,self.session->term->rows-1,true);self.session->painting=false;}}
- (void)sendText:(NSString *)text{MacSession *s=self.session;if(!s||!s->backend||s->input_locked)return;NSData *data=[text dataUsingEncoding:NSUTF32LittleEndianStringEncoding];term_keyinputw(s->term,data.bytes,data.length/sizeof(wchar_t));}
- (void)insertText:(id)value replacementRange:(NSRange)range{NSString *text=[value isKindOfClass:NSAttributedString.class]?[value string]:value;[self unmarkText];[self sendText:text];}
- (void)loadBindings {
    self.bindings=[NSMutableArray new];NSArray *actions=@[@[@"script",@"{CONTROL}{F2}",@"script:"],@[@"sendfile",@"{CONTROL}{F3}",@"upload:"],@[@"getfile",@"{CONTROL}{F4}",@"download:"],@[@"tray",@"{CONTROL}{F6}",@"hideWindow:"],@[@"visible",@"{CONTROL}{F7}",@"toggleTop:"],@[@"protect",@"{CONTROL}{F9}",@"toggleLock:"],@[@"autocommand",@"{SHIFT}{F12}",@"startup:"],@[@"eventlog",@"",@"showLog:"],@[@"copyall",@"",@"copyAll:"],@[@"duplicate",@"",@"duplicate:"],@[@"opennew",@"",@"showBrowser:"],@[@"changesettings",@"",@"settings:"],@[@"fullscreen",@"",@"fullscreen:"],@[@"clearscrollback",@"",@"clearHistory:"],@[@"resetterminal",@"",@"resetTerminal:"]];
    for(NSArray *action in actions){char *value=kr_mac_ini("Shortcuts",[action[0]UTF8String],[action[1]UTF8String]);unsigned key=kr_mac_parse_shortcut(value);sfree(value);if(key)[self.bindings addObject:@{@"key":@(key),@"action":action[2]}];}
    char *list=kr_mac_ini("Shortcuts","list",""),*cursor=list,*name;while((name=strsep(&cursor," ;\t\r\n"))&&self.bindings.count<512){unsigned key=kr_mac_parse_shortcut(name);if(!key)continue;char *value=kr_mac_ini("Shortcuts",name,"");if(*value){char *text=kr_mac_command_text(value,true,false);NSString *decoded=[NSString stringWithUTF8String:text];if(decoded)[self.bindings addObject:@{@"key":@(key),@"text":decoded}];smemclr(text,strlen(text));sfree(text);}smemclr(value,strlen(value));sfree(value);}sfree(list);
}
- (BOOL)shortcut:(NSEvent *)event {
    MacSession *s=self.session;if(!s||!conf_get_bool(s->conf,CONF_kr_shortcuts))return NO;unsigned key=0,mods=0;NSEventModifierFlags flags=event.modifierFlags;if(flags&NSEventModifierFlagControl)mods|=2;if(flags&NSEventModifierFlagShift)mods|=4;if(flags&NSEventModifierFlagOption)mods|=1;if(flags&NSEventModifierFlagCommand)mods|=8;
    if((mods&3)==3&&!conf_get_bool(s->conf,CONF_disablealtgr))mods=(mods&~3)|16;
    unichar c=event.charactersIgnoringModifiers.length?[event.charactersIgnoringModifiers characterAtIndex:0]:0;
    if(c>=NSF1FunctionKey&&c<=NSF24FunctionKey)key=112+c-NSF1FunctionKey;
    else {static const unsigned physical[128]={[0]='A',[1]='S',[2]='D',[3]='F',[4]='H',[5]='G',[6]='Z',[7]='X',[8]='C',[9]='V',[11]='B',[12]='Q',[13]='W',[14]='E',[15]='R',[16]='Y',[17]='T',[31]='O',[32]='U',[34]='I',[35]='P',[37]='L',[38]='J',[40]='K',[45]='N',[46]='M',[36]=13,[76]=13,[53]=27,[49]=32,[51]=8,[48]=9,[114]=45,[117]=46,[115]=36,[119]=35,[126]=38,[125]=40,[123]=37,[124]=39,[116]=33,[121]=34,[24]=187,[27]=189,[43]=188,[47]=190};key=event.keyCode<128?physical[event.keyCode]:0;if(!key&&c<128)key=toupper(c);}
    unsigned code=key|(mods<<16);for(NSDictionary *binding in self.bindings)if([binding[@"key"]unsignedIntValue]==code){NSString *action=binding[@"action"];if(s->input_locked&&![action isEqualToString:@"toggleLock:"])return YES;if(action)[self tryToPerform:NSSelectorFromString(action) with:nil];else if(s->started&&s->backend&&backend_sendok(s->backend))[self sendText:binding[@"text"]];return YES;}return NO;
}
- (void)copyAll:(id)sender{if(self.session){int clip=CLIP_CLIPBOARD;term_copyall(self.session->term,&clip,1);}}
- (void)clearHistory:(id)sender{if(self.session)term_clrsb(self.session->term);}
- (void)resetTerminal:(id)sender{if(self.session)term_pwron(self.session->term,true);}
- (void)duplicate:(id)sender{if(self.session)kr_mac_open_conf(self.session->conf);}
- (void)showBrowser:(id)sender{kr_mac_show_windows();}
- (void)keyDown:(NSEvent *)event{MacSession *s=self.session;if(!s||!s->term)return;if([self shortcut:event])return;
    if((event.modifierFlags&NSEventModifierFlagCommand)&&(event.keyCode==37||[event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"l"])){s->input_locked=!s->input_locked;return;}if(s->input_locked)return;
    if(event.modifierFlags&NSEventModifierFlagCommand){NSString *key=event.charactersIgnoringModifiers.lowercaseString;if(event.keyCode==8||[key isEqualToString:@"c"])[self copy:nil];else if(event.keyCode==9||[key isEqualToString:@"v"])[self paste:nil];return;}
    if(self.hasMarkedText){[self interpretKeyEvents:@[event]];return;}
    NSString *characters=event.charactersIgnoringModifiers;if(!characters.length)return;unichar key=[characters characterAtIndex:0];char buffer[128];int n=0;bool consumed=false,shift=(event.modifierFlags&NSEventModifierFlagShift)!=0,control=(event.modifierFlags&NSEventModifierFlagControl)!=0,alt=(event.modifierFlags&NSEventModifierFlagOption)!=0;
    if(key==NSUpArrowFunctionKey||key==NSDownArrowFunctionKey||key==NSLeftArrowFunctionKey||key==NSRightArrowFunctionKey)n=format_arrow_key(buffer,s->term,key==NSUpArrowFunctionKey?'A':key==NSDownArrowFunctionKey?'B':key==NSRightArrowFunctionKey?'C':'D',shift,control,alt,&consumed);
    else if(key>=NSF1FunctionKey&&key<=NSF24FunctionKey)n=format_function_key(buffer,s->term,key-NSF1FunctionKey+1,shift,control,alt,&consumed);
    else if(key==NSHomeFunctionKey||key==NSEndFunctionKey||key==NSPageUpFunctionKey||key==NSPageDownFunctionKey||key==NSDeleteFunctionKey||key==NSInsertFunctionKey)n=format_small_keypad_key(buffer,s->term,key==NSHomeFunctionKey?SKK_HOME:key==NSEndFunctionKey?SKK_END:key==NSPageUpFunctionKey?SKK_PGUP:key==NSPageDownFunctionKey?SKK_PGDN:key==NSDeleteFunctionKey?SKK_DELETE:SKK_INSERT,shift,control,alt,&consumed);
    else if(event.keyCode==36||event.keyCode==76){buffer[0]='\r';n=1;}else if(event.keyCode==51){buffer[0]=conf_get_bool(s->conf,CONF_bksp_is_delete)?127:8;n=1;}else if(event.keyCode==53){buffer[0]=27;n=1;}else if(event.keyCode==48){if(shift){memcpy(buffer,"\033[Z",3);n=3;}else{buffer[0]='\t';n=1;}}
    if(n){if(alt&&!consumed)term_keyinput(s->term,CS_UTF8,"\033",1);term_keyinput(s->term,CS_UTF8,buffer,n);return;}
    if(control&&key>=128){static const char physical[128]={[0]='A',[1]='S',[2]='D',[3]='F',[4]='H',[5]='G',[6]='Z',[7]='X',[8]='C',[9]='V',[11]='B',[12]='Q',[13]='W',[14]='E',[15]='R',[16]='Y',[17]='T',[31]='O',[32]='U',[34]='I',[35]='P',[37]='L',[38]='J',[40]='K',[45]='N',[46]='M'};if(event.keyCode<128&&physical[event.keyCode])key=physical[event.keyCode];}
    if(control&&key<128){char c=toupper(key);if(c>='@'&&c<='_')c&=31;else if(c=='?')c=127;term_keyinput(s->term,CS_UTF8,&c,1);return;}
    [self interpretKeyEvents:@[event]];
}
- (void)copy:(id)sender{int clip=CLIP_CLIPBOARD;if(self.session)term_request_copy(self.session->term,&clip,1);}
- (void)paste:(id)sender{if(!self.session||self.session->input_locked)return;NSString *text=[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];if(text){NSData *data=[text dataUsingEncoding:NSUTF32LittleEndianStringEncoding];term_do_paste(self.session->term,data.bytes,data.length/sizeof(wchar_t));}}
- (BOOL)openLink:(NSEvent *)event {
    MacSession *s=self.session;if(!s||!conf_get_bool(s->conf,CONF_kr_hyperlinks)||!(event.modifierFlags&NSEventModifierFlagControl))return NO;
    NSPoint point=[self convertPoint:event.locationInWindow fromView:nil];int row=point.y/self.cellHeight,col=point.x/self.cellWidth;if(row<0||row>=s->term->rows||col<0||col>=s->term->cols)return NO;
    termline *line=s->term->disptext[row];NSMutableString *text=[NSMutableString new];NSUInteger target=0,last=0;
    for(int i=0;i<s->term->cols;i++){unsigned c=line->chars[i].chr;if(i==col)target=c==UCSWIDE?last:text.length;if(c==UCSWIDE)continue;last=text.length;wchar_t value=c;[text appendString:run_string(s,&value,1)];}
    NSRegularExpression *regex=[NSRegularExpression regularExpressionWithPattern:@"https?://[^\\s<>\"']+" options:NSRegularExpressionCaseInsensitive error:nil];
    for(NSTextCheckingResult *match in [regex matchesInString:text options:0 range:NSMakeRange(0,text.length)]){
        NSRange range=match.range;while(range.length&&[[NSCharacterSet characterSetWithCharactersInString:@".,;:!?)]}"]characterIsMember:[text characterAtIndex:NSMaxRange(range)-1]])range.length--;
        if(target<range.location||target>=NSMaxRange(range))continue;NSURL *url=[NSURL URLWithString:[text substringWithRange:range]];if(url.host.length&&([url.scheme.lowercaseString isEqualToString:@"http"]||[url.scheme.lowercaseString isEqualToString:@"https"])){[NSWorkspace.sharedWorkspace openURL:url];return YES;}
    }return NO;
}
- (void)mouseDown:(NSEvent *)event{if([self openLink:event])return;[self.window makeFirstResponder:self];[self mouse:event action:MA_CLICK];if(event.clickCount>=2)[self mouse:event action:event.clickCount==2?MA_2CLK:MA_3CLK];}
- (void)rightMouseDown:(NSEvent *)event{if(self.session&&self.session->raw_mouse&&!(event.modifierFlags&NSEventModifierFlagShift))[self mouse:event action:MA_CLICK];else [NSMenu popUpContextMenu:[self menuForEvent:event] withEvent:event forView:self];}
- (void)rightMouseUp:(NSEvent *)event{if(self.session&&self.session->raw_mouse)[self mouse:event action:MA_RELEASE];}
- (void)rightMouseDragged:(NSEvent *)event{if(self.session&&self.session->raw_mouse)[self mouse:event action:MA_DRAG];}
- (void)otherMouseDown:(NSEvent *)event{[self mouse:event action:MA_CLICK];}
- (void)otherMouseUp:(NSEvent *)event{[self mouse:event action:MA_RELEASE];}
- (void)otherMouseDragged:(NSEvent *)event{[self mouse:event action:MA_DRAG];}
- (void)mouseDragged:(NSEvent *)event{[self mouse:event action:MA_DRAG];}
- (void)mouseUp:(NSEvent *)event{[self mouse:event action:MA_RELEASE];}
- (void)mouse:(NSEvent *)event action:(Mouse_Action)action{MacSession *s=self.session;if(!s||(s->input_locked&&s->raw_mouse))return;NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];Mouse_Button physical=event.buttonNumber==1?MBT_RIGHT:event.buttonNumber==2?MBT_MIDDLE:MBT_LEFT,logical=physical==MBT_RIGHT?MBT_EXTEND:physical==MBT_MIDDLE?MBT_PASTE:MBT_SELECT;term_mouse(s->term,physical,logical,action,p.x/self.cellWidth,p.y/self.cellHeight,(event.modifierFlags&NSEventModifierFlagShift)!=0,(event.modifierFlags&NSEventModifierFlagControl)!=0,(event.modifierFlags&NSEventModifierFlagOption)!=0);[self setNeedsDisplay:YES];}
- (void)scrollWheel:(NSEvent *)event{MacSession *s=self.session;if(!s||event.scrollingDeltaY==0)return;if(s->raw_mouse){if(s->input_locked)return;NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];Mouse_Button b=event.scrollingDeltaY>0?MBT_WHEEL_UP:MBT_WHEEL_DOWN;term_mouse(s->term,b,b,MA_CLICK,p.x/self.cellWidth,p.y/self.cellHeight,false,false,false);}else{int setting=conf_get_int(s->conf,CONF_scrolllines),unit=setting==-2?s->term->rows:setting==-1?MAX(1,s->term->rows/2):setting>0?MIN(setting,10000):3;int lines=event.hasPreciseScrollingDeltas?MAX(1,MIN(20,fabs(event.scrollingDeltaY))):unit;term_scroll(s->term,0,event.scrollingDeltaY>0?-lines:lines);}[self setNeedsDisplay:YES];}
- (BOOL)hasMarkedText{return self.marked.length>0;}
- (NSRange)markedRange{return self.marked.length?NSMakeRange(0,self.marked.length):NSMakeRange(NSNotFound,0);}
- (NSRange)selectedRange{return NSMakeRange(NSNotFound,0);}
- (void)setMarkedText:(id)value selectedRange:(NSRange)selected replacementRange:(NSRange)range{self.marked=[value isKindOfClass:NSAttributedString.class]?[[NSMutableAttributedString alloc]initWithAttributedString:value]:[[NSMutableAttributedString alloc]initWithString:value];if(self.session)term_set_preedit_text(self.session->term,(char *)self.marked.string.UTF8String);}
- (void)unmarkText{[self.marked setAttributedString:[[NSAttributedString alloc]initWithString:@""]];if(self.session)term_set_preedit_text(self.session->term,NULL);}
- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText{return @[NSUnderlineStyleAttributeName];}
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual{return nil;}
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual{int x=0,y=0;if(self.session)term_get_cursor_position(self.session->term,&x,&y);NSRect rect=[self convertRect:NSMakeRect(x*self.cellWidth,y*self.cellHeight,self.cellWidth,self.cellHeight) toView:nil];return [self.window convertRectToScreen:rect];}
- (NSUInteger)characterIndexForPoint:(NSPoint)point{return NSNotFound;}
- (void)doCommandBySelector:(SEL)selector{if(selector==@selector(insertNewline:))[self sendText:@"\r"];else if(selector==@selector(insertTab:))[self sendText:@"\t"];else if(selector==@selector(deleteBackward:))[self sendText:@"\177"];}
- (void)windowDidBecomeKey:(NSNotification *)note{if(self.session)term_set_focus(self.session->term,true);}
- (void)windowDidResignKey:(NSNotification *)note{if(self.session)term_set_focus(self.session->term,false);}
- (void)windowWillClose:(NSNotification *)note{MacSession *s=self.session;if(s){s->closed=true;s->started=false;s->reconnect_due=0;cancel_commands(s);[self saveWindow];[NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self];if(s->ldisc)ldisc_free(s->ldisc);if(s->backend)backend_free(s->backend);if(s->term)term_free(s->term);if(s->logctx)log_free(s->logctx);conf_free(s->conf);delete_callbacks_for_context(s);expire_timer_context(s);sfree(s);self.session=NULL;}self.terminalWindow.delegate=nil;self.terminalWindow=nil;[windows removeObject:self];}
@end
void kr_mac_pump(void){if(pumping||!initialized)return;pumping=true;fd_set read,write,except;FD_ZERO(&read);FD_ZERO(&write);FD_ZERO(&except);int state,flags,fd,maxfd=-1;
    for(fd=first_fd(&state,&flags);fd>=0;fd=next_fd(&state,&flags)){if(fd>=FD_SETSIZE)continue;if(flags&1)FD_SET(fd,&read);if(flags&2)FD_SET(fd,&write);if(flags&4)FD_SET(fd,&except);maxfd=MAX(maxfd,fd);}
    struct timeval timeout={0,0};if(maxfd>=0&&select(maxfd+1,&read,&write,&except,&timeout)>0)for(fd=0;fd<=maxfd;fd++){if(FD_ISSET(fd,&read))select_result(fd,1);if(FD_ISSET(fd,&write))select_result(fd,2);if(FD_ISSET(fd,&except))select_result(fd,4);}
    unsigned long next;run_timers(GETTICKCOUNT(),&next);run_toplevel_callbacks();for(MacTerminal *view in [windows copy])[view service];pumping=false;
}
bool kr_mac_select_root(const char *path){char *resolved=realpath(path,NULL);if(!resolved){message("Portable directory is unavailable");return false;}bool different=kr_mac_root()&&strcmp(kr_mac_root(),resolved);if(windows.count&&different){free(resolved);message("Close existing connections before changing the portable directory");return false;}kr_mac_set_root(resolved);bool selected=kr_mac_root()&&!strcmp(kr_mac_root(),resolved);free(resolved);if(!selected)message("Choose a portable directory outside the application bundle");return selected;}
void kr_mac_open_session(const char *name,const char *path){if(!kr_mac_select_root(path))return;
    Conf *conf=conf_new();bool exists=load_settings(name,conf);conf_set_str(conf,CONF_kr_session_name,name);
    if(!exists||!conf_launchable(conf)){conf_free(conf);message("Unable to load a connectable session. Select a saved profile with a host name.");return;}
    kr_mac_open_conf(conf);conf_free(conf);
}
void kr_mac_open_conf(Conf *configuration){
    if(!initialized){setlocale(LC_CTYPE,"");sk_init();uxsel_init();enable_dit();settings_set_default_protocol(PROT_SSH);settings_set_default_port(22);windows=[NSMutableArray new];initialized=true;}
    Conf *conf=conf_copy(configuration);const char *name=conf_get_str(conf,CONF_kr_session_name);
    MacTerminal *view=[[MacTerminal alloc]initWithFrame:NSMakeRect(0,0,800,480)];MacSession *s=snew(MacSession);memset(s,0,sizeof(*s));s->conf=conf;view.session=s;char *profile=kr_mac_profile_path(name);view.profileSource=profile?[NSData dataWithContentsOfFile:S(profile)]:nil;sfree(profile);
    NSWindow *window=[[NSWindow alloc]initWithContentRect:view.bounds styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];window.title=[@"KiTTY-ng — " stringByAppendingString:S(*name?name:conf_get_str(conf,CONF_host))];MacTerminalFrame *frame=[[MacTerminalFrame alloc]initWithFrame:view.bounds];
    window.contentView=frame;view.frame=NSInsetRect(frame.bounds,terminalPadding,terminalPadding);[frame addSubview:view];window.releasedWhenClosed=NO;window.delegate=view;window.minSize=NSMakeSize(240,160);view.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;view.terminalWindow=window;[windows addObject:view];[view start];[window center];[window makeKeyAndOrderFront:nil];[window makeFirstResponder:view];
    if(conf_get_bool(conf,CONF_save_windowpos)){settings_r *reader=open_settings_r(name);int x=read_setting_i(reader,"MacWindowX",INT_MIN),y=read_setting_i(reader,"MacWindowY",INT_MIN),w=read_setting_i(reader,"MacWindowWidth",0),h=read_setting_i(reader,"MacWindowHeight",0);close_settings_r(reader);if(x!=INT_MIN&&y!=INT_MIN&&w>=240&&h>=160&&w<=10000&&h<=10000)[window setFrame:NSMakeRect(x,y,w,h) display:YES];}
    if(conf_get_bool(conf,CONF_maximize))[window zoom:nil];if(conf_get_bool(conf,CONF_fullscreen))[window toggleFullScreen:nil];if(conf_get_bool(conf,CONF_sendtotray))[window orderOut:nil];
}

void kr_mac_show_windows(void){for(MacTerminal *view in [windows copy])[view.terminalWindow makeKeyAndOrderFront:nil];for(NSWindow *window in NSApp.windows)if([window.title isEqualToString:@"KiTTY-ng"])[window makeKeyAndOrderFront:nil];}
