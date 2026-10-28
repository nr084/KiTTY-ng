/* AppKit session host. Each page owns its Conf, terminal and SSH backend. */
static const CGFloat macTabHeight=34, macTabWidth=184;
static NSMutableArray *tabHosts;
static BOOL macTabsEnabled(void){
    char *value=kr_mac_ini("KiTTY","SessionTabs","1");
    BOOL enabled=strcasecmp(value,"0")&&strcasecmp(value,"no")&&strcasecmp(value,"false")&&strcasecmp(value,"off");
    sfree(value);return enabled;
}
@interface MacTabHost : NSObject <NSWindowDelegate>
@property(nonatomic,strong) NSWindow *window;
@property(nonatomic,strong) NSMutableArray<MacTerminal*> *pages;
@property(nonatomic,strong) MacTerminal *selected;
@property(nonatomic,strong) NSScrollView *strip;
@property BOOL tabsEnabled,closing;
- (void)addPage:(MacTerminal*)page;
- (void)selectPage:(MacTerminal*)page;
- (void)movePage:(MacTerminal*)page to:(NSInteger)index;
- (void)closePage:(MacTerminal*)page;
- (void)refresh;
- (void)layout;
@end
@interface MacTabsFrame : MacTerminalFrame
@property(nonatomic,weak) MacTabHost *host;
@end
@implementation MacTabsFrame
- (void)resizeSubviewsWithOldSize:(NSSize)size{[self.host layout];}
@end
@interface MacTabsDocument : NSView
@end
@implementation MacTabsDocument
- (BOOL)isFlipped{return YES;}
@end
@interface MacTabButton : NSButton
@property(nonatomic,weak) MacTabHost *host;
@property(nonatomic,weak) MacTerminal *page;
@property BOOL closeControl,hovered;
@property(nonatomic,strong) NSTrackingArea *hoverTracking;
@end
@implementation MacTabButton
- (BOOL)wantsUpdateLayer{return NO;}
- (BOOL)isFlipped{return YES;}
- (BOOL)acceptsFirstResponder{return NO;}
- (void)updateTrackingAreas{[super updateTrackingAreas];if(self.hoverTracking)[self removeTrackingArea:self.hoverTracking];self.hoverTracking=[[NSTrackingArea alloc]initWithRect:NSZeroRect options:NSTrackingMouseEnteredAndExited|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect owner:self userInfo:nil];[self addTrackingArea:self.hoverTracking];}
- (void)mouseEntered:(NSEvent*)event{self.hovered=YES;self.needsDisplay=YES;}
- (void)mouseExited:(NSEvent*)event{self.hovered=NO;self.needsDisplay=YES;}
- (void)drawRect:(NSRect)dirty{
    if(self.closeControl){
        [(self.hovered||self.highlighted?[NSColor colorWithSRGBRed:0.87 green:0.23 blue:0.25 alpha:1]:NSColor.secondaryLabelColor)setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(self.bounds,3,3)]fill];
        [NSColor.whiteColor setStroke];NSBezierPath *cross=[NSBezierPath bezierPath];cross.lineWidth=1.5;
        CGFloat x=NSMidX(self.bounds),y=NSMidY(self.bounds);
        [cross moveToPoint:NSMakePoint(x-2.5,y-2.5)];[cross lineToPoint:NSMakePoint(x+2.5,y+2.5)];
        [cross moveToPoint:NSMakePoint(x+2.5,y-2.5)];[cross lineToPoint:NSMakePoint(x-2.5,y+2.5)];[cross stroke];return;
    }
    BOOL active=self.host.selected==self.page;
    [(active?[NSColor colorWithSRGBRed:0.18 green:0.46 blue:0.72 alpha:1]:NSColor.controlBackgroundColor)setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds,2,3) xRadius:6 yRadius:6]fill];
    NSMutableParagraphStyle *style=[NSMutableParagraphStyle new];style.lineBreakMode=NSLineBreakByTruncatingTail;
    [self.title drawInRect:NSMakeRect(12,9,self.bounds.size.width-42,18) withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:12 weight:active?NSFontWeightSemibold:NSFontWeightRegular],NSForegroundColorAttributeName:active?NSColor.whiteColor:NSColor.labelColor,NSParagraphStyleAttributeName:style}];
}
- (void)clicked:(id)sender{if(self.closeControl)[self.host closePage:self.page];else[self.host selectPage:self.page];}
- (void)mouseDown:(NSEvent*)event{
    if(self.closeControl){[super mouseDown:event];return;}
    MacTabHost *host=self.host;MacTerminal *page=self.page;NSWindow *window=host.window;
    NSPoint start=event.locationInWindow;[host selectPage:page];
    while(host.pages.count>1&&[host.pages containsObject:page]){
        NSEvent *next=[window nextEventMatchingMask:NSEventMaskLeftMouseDragged|NSEventMaskLeftMouseUp];
        if(!next||next.type==NSEventTypeLeftMouseUp)break;
        if(fabs(next.locationInWindow.x-start.x)<4)continue;
        NSPoint p=[host.strip.documentView convertPoint:next.locationInWindow fromView:nil];
        NSInteger index=MAX(0,MIN((NSInteger)host.pages.count-1,(NSInteger)floor(p.x/macTabWidth)));
        [host movePage:page to:index];
    }
    [window makeFirstResponder:host.selected];
}
- (NSMenu*)menuForEvent:(NSEvent*)event{
    MacTabHost *host=self.host;NSInteger index=[host.pages indexOfObject:self.page];
    NSMenu *menu=[NSMenu new];menu.autoenablesItems=NO;menu.allowsContextMenuPlugIns=NO;
    NSArray *commands=@[@[T(@"Close tab",@"Закрыть вкладку"),@"closeTab:"],@[T(@"Duplicate session",@"Дублировать сеанс"),@"duplicateTab:"],@[T(@"Move left",@"Переместить влево"),@"moveLeft:"],@[T(@"Move right",@"Переместить вправо"),@"moveRight:"]];
    for(NSUInteger i=0;i<commands.count;i++){
        NSMenuItem *item=[[NSMenuItem alloc]initWithTitle:commands[i][0] action:NSSelectorFromString(commands[i][1]) keyEquivalent:@""];
        item.target=host;item.representedObject=self.page;item.enabled=i<2||(i==2?index>0:index+1<(NSInteger)host.pages.count);[menu addItem:item];
    }return menu;
}
@end
@implementation MacTabHost
- (void)duplicateTab:(NSMenuItem*)item{MacTerminal *page=item.representedObject;if([self.pages containsObject:page])[page duplicate:nil];}
- (void)closeTab:(NSMenuItem*)item{[self closePage:item.representedObject];}
- (void)moveLeft:(NSMenuItem*)item{[self movePage:item.representedObject to:[self.pages indexOfObject:item.representedObject]-1];}
- (void)moveRight:(NSMenuItem*)item{[self movePage:item.representedObject to:[self.pages indexOfObject:item.representedObject]+1];}
- (instancetype)init{
    if(!(self=[super init]))return nil;
    self.pages=[NSMutableArray new];self.tabsEnabled=macTabsEnabled();
    self.window=[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,800,480) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
    self.window.tabbingMode=NSWindowTabbingModeDisallowed;self.window.releasedWhenClosed=NO;self.window.minSize=NSMakeSize(240,160);self.window.delegate=self;
    MacTabsFrame *frame=[[MacTabsFrame alloc]initWithFrame:self.window.contentView.bounds];frame.host=self;self.window.contentView=frame;
    self.strip=[[NSScrollView alloc]initWithFrame:NSZeroRect];self.strip.drawsBackground=YES;self.strip.backgroundColor=NSColor.windowBackgroundColor;self.strip.hasHorizontalScroller=YES;self.strip.autohidesScrollers=YES;
    MacTabsDocument *document=[[MacTabsDocument alloc]initWithFrame:NSZeroRect];self.strip.documentView=document;[frame addSubview:self.strip];
    if(!tabHosts)tabHosts=[NSMutableArray new];[tabHosts addObject:self];return self;
}
- (void)addPage:(MacTerminal*)page{
    page.host=self;page.terminalWindow=self.window;[self.pages addObject:page];[self.window.contentView addSubview:page];[self selectPage:page];
}
- (void)selectPage:(MacTerminal*)page{
    if(![self.pages containsObject:page])return;
    if(self.selected.session&&self.selected.session->term)term_set_focus(self.selected.session->term,false);
    self.selected=page;[self refresh];
    [self.window makeFirstResponder:page];
    if(page.session&&page.session->term){term_set_focus(page.session->term,self.window.keyWindow);term_invalidate(page.session->term);}
    if(page.session){self.window.level=conf_get_bool(page.session->conf,CONF_alwaysontop)?NSFloatingWindowLevel:NSNormalWindowLevel;int alpha=conf_get_int(page.session->conf,CONF_transparencynumber);self.window.alphaValue=alpha<0?1:MAX(0.1,1.0-MIN(alpha,255)/255.0);}
    if(self.pages.count>1){NSUInteger i=[self.pages indexOfObject:page];[self.strip.documentView scrollRectToVisible:NSMakeRect(i*macTabWidth,0,macTabWidth,macTabHeight)];}
}
- (void)layout{
    NSRect bounds=self.window.contentView.bounds;CGFloat top=self.pages.count>1?macTabHeight:0;
    self.strip.hidden=top==0;self.strip.frame=NSMakeRect(0,0,bounds.size.width,top);
    self.strip.documentView.frame=NSMakeRect(0,0,MAX(bounds.size.width,self.pages.count*macTabWidth),macTabHeight);
    [self.strip tile];[self.strip.documentView setNeedsDisplay:YES];for(NSView *button in self.strip.documentView.subviews)[button setNeedsDisplay:YES];
    for(MacTerminal *page in self.pages){page.hidden=page!=self.selected;page.frame=NSInsetRect(NSMakeRect(0,top,bounds.size.width,MAX(1,bounds.size.height-top)),terminalPadding,terminalPadding);}
}
- (void)refresh{
    self.window.title=self.selected.sessionTitle?:@"KiTTY-ng";
    for(NSView *view in [self.strip.documentView.subviews copy])[view removeFromSuperview];
    [self.pages enumerateObjectsUsingBlock:^(MacTerminal *page,NSUInteger index,BOOL *stop){
        MacTabButton *button=[[MacTabButton alloc]initWithFrame:NSMakeRect(index*macTabWidth,0,macTabWidth,macTabHeight)];button.host=self;button.page=page;button.title=[page.sessionTitle hasPrefix:@"KiTTY-ng — "]?[page.sessionTitle substringFromIndex:[@"KiTTY-ng — " length]]:page.sessionTitle?:@"KiTTY-ng";button.bordered=NO;button.target=button;button.action=@selector(clicked:);button.accessibilityLabel=page.sessionTitle;button.accessibilityIdentifier=@"KiTTYNG.SessionTab";
        MacTabButton *close=[[MacTabButton alloc]initWithFrame:NSMakeRect((index+1)*macTabWidth-28,7,20,20)];close.host=self;close.page=page;close.closeControl=YES;close.title=T(@"Close tab",@"Закрыть вкладку");close.accessibilityLabel=[NSString stringWithFormat:@"%@ — %@",close.title,button.title];close.accessibilityIdentifier=@"KiTTYNG.CloseTab";close.bordered=NO;close.target=close;close.action=@selector(clicked:);[self.strip.documentView addSubview:button];[self.strip.documentView addSubview:close];
    }];[self layout];
}
- (void)movePage:(MacTerminal*)page to:(NSInteger)index{
    NSUInteger old=[self.pages indexOfObject:page];if(old==NSNotFound||index<0||index>=(NSInteger)self.pages.count||old==(NSUInteger)index)return;
    [self.pages removeObjectAtIndex:old];[self.pages insertObject:page atIndex:index];[self refresh];[self.window makeFirstResponder:self.selected];
}
- (void)closePage:(MacTerminal*)page{
    NSUInteger index=[self.pages indexOfObject:page];if(index==NSNotFound||self.closing)return;
    BOOL active=self.selected==page;if(active){[self.window makeFirstResponder:nil];self.selected=nil;}
    [page disposeSession];[page removeFromSuperview];[self.pages removeObjectAtIndex:index];
    if(!self.pages.count){[self.window close];return;}
    [self selectPage:active?self.pages[MIN(index,self.pages.count-1)]:self.selected];
}
- (BOOL)windowShouldClose:(NSWindow*)window{
    BOOL warn=NO;for(MacTerminal *page in self.pages)if(page.session&&!page.session->closed&&conf_get_bool(page.session->conf,CONF_warn_on_close))warn=YES;
    if(!warn)return YES;
    NSAlert *alert=[NSAlert new];alert.messageText=T(@"Close this window and all its sessions?",@"Закрыть окно и все его сеансы?");[alert addButtonWithTitle:T(@"Cancel",@"Отмена")];[alert addButtonWithTitle:T(@"Close",@"Закрыть")];BOOL close=[alert runModal]==NSAlertSecondButtonReturn;if(!close)[self.window makeFirstResponder:self.selected];return close;
}
- (void)windowDidBecomeKey:(NSNotification*)note{if(self.selected.session&&self.selected.session->term)term_set_focus(self.selected.session->term,true);}
- (void)windowDidResignKey:(NSNotification*)note{if(self.selected.session&&self.selected.session->term)term_set_focus(self.selected.session->term,false);}
- (void)windowWillClose:(NSNotification*)note{
    self.closing=YES;for(MacTerminal *page in [self.pages copy]){[page disposeSession];[page removeFromSuperview];}self.selected=nil;[self.pages removeAllObjects];self.window.delegate=nil;[tabHosts removeObject:self];
}
@end

static NSColor *macTestCloseColour(MacTabButton *button){
    NSBitmapImageRep *bitmap=[[NSBitmapImageRep alloc]initWithBitmapDataPlanes:NULL pixelsWide:20 pixelsHigh:20 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    [NSGraphicsContext saveGraphicsState];NSGraphicsContext.currentContext=[NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];[button drawRect:button.bounds];[NSGraphicsContext restoreGraphicsState];
    return [[bitmap colorAtX:5 y:10]colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];
}
int kr_mac_test_tabs_ui(void){
#define TAB_CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL tabs: %s\n",#c);return 1;}}while(0)
    MacTabButton *close=[[MacTabButton alloc]initWithFrame:NSMakeRect(0,0,20,20)];close.closeControl=YES;
    NSColor *gray=macTestCloseColour(close);
    [close mouseEntered:nil];TAB_CHECK(close.hovered);
    NSColor *red=macTestCloseColour(close);
    TAB_CHECK(fabs(gray.redComponent-gray.greenComponent)<0.12&&red.redComponent>red.greenComponent+0.3);
    [close mouseExited:nil];TAB_CHECK(!close.hovered);
    MacTabHost *host=[MacTabHost new];
    MacTerminal *a=[MacTerminal new],*b=[MacTerminal new],*c=[MacTerminal new];
    a.sessionTitle=@"Synthetic A";b.sessionTitle=@"Synthetic B";c.sessionTitle=@"Synthetic C";
    [host addPage:a];TAB_CHECK(host.strip.hidden&&host.selected==a&&!a.hidden);
    [host addPage:b];[host addPage:c];TAB_CHECK(!host.strip.hidden&&host.selected==c&&a.hidden&&b.hidden&&!c.hidden);
    TAB_CHECK(a.terminalWindow==c.terminalWindow&&host.strip.documentView.subviews.count==6);
    [host movePage:a to:2];TAB_CHECK(host.selected==c&&host.pages[2]==a);
    [host closePage:b];TAB_CHECK(host.selected==c&&host.pages.count==2&&c.terminalWindow==host.window);
    [host closePage:c];TAB_CHECK(host.selected==a&&host.pages.count==1&&host.strip.hidden&&!a.hidden);
    [host closePage:a];TAB_CHECK(host.pages.count==0&&a.host==nil&&a.terminalWindow==nil);
    NSString *path=[S(kr_mac_root()) stringByAppendingPathComponent:@"kitty.ini"];
    NSData *original=[NSData dataWithContentsOfFile:path];
    for(NSString *value in @[@"0",@"off",@"false",@"no"]){
        [[NSString stringWithFormat:@"[KiTTY]\nSessionTabs=%@\n",value]writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];TAB_CHECK(!macTabsEnabled());
    }
    [@"[KiTTY]\nSessionTabs=1\n" writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];TAB_CHECK(macTabsEnabled());
    if(original)[original writeToFile:path atomically:YES];else[NSFileManager.defaultManager removeItemAtPath:path error:nil];
    puts("PASS: native tabs hide/show, independent pages, active-preserving reorder/close, final-page teardown, SessionTabs INI");return 0;
#undef TAB_CHECK
}
