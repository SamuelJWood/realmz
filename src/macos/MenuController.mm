#import "../MenuController.h"
#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#include <cstddef>
#include <cstdint>
#include <objc/NSObject.h>
#include <resource_file/ResourceFile.hh>

NSMenu* MCCreateMenu(const MenuList& menuList);
NSMenu* MCCreateSubMenu(NSString* title, const Menu& menuRes, const std::list<std::shared_ptr<Menu>> submenus);

@interface MCMenuItemIdentifier : NSObject

@property(readonly) int16_t menuID;
@property(readonly) int16_t itemID;
@property(readonly, nullable) NSString* itemDescription;

@end

@implementation MCMenuItemIdentifier

- (id)initWithRawIds:(int16_t)menu_id itemId:(int16_t)item_id {
  return [self initWithRawIds:menu_id itemId:item_id description:nil];
}

- (id)initWithRawIds:(int16_t)menu_id itemId:(int16_t)item_id description:(nullable NSString*)desc {
  if (self = [super init]) {
    _menuID = menu_id;
    _itemID = item_id;
    _itemDescription = [desc copy];
  }
  return self;
}

@end

// === Description hover popup delegate ===

@interface MCDescriptionPanel : NSObject
- (void)showWithText:(NSString*)text;
- (void)hide;
@end

@implementation MCDescriptionPanel {
  NSWindow* _window;
  NSTextView* _textView;
}

- (instancetype)init {
  if (self = [super init]) {
    NSRect frame = NSMakeRect(0, 0, 380, 100);
    _window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskBorderless
                    backing:NSBackingStoreBuffered
                      defer:YES];
    [_window setLevel:NSPopUpMenuWindowLevel + 1];
    [_window setIgnoresMouseEvents:YES];
    [_window setHasShadow:YES];
    [_window setOpaque:NO];
    [_window setBackgroundColor:[NSColor colorWithRed:1.0 green:0.99 blue:0.82 alpha:1.0]];

    _window.contentView.wantsLayer = YES;
    _window.contentView.layer.borderColor = [[NSColor grayColor] CGColor];
    _window.contentView.layer.borderWidth = 1.0;
    _window.contentView.layer.cornerRadius = 3.0;

    NSScrollView* sv = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 380, 100)];
    sv.hasVerticalScroller = NO;
    sv.hasHorizontalScroller = NO;
    sv.borderType = NSNoBorder;
    sv.drawsBackground = NO;
    sv.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    _textView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 380, 100)];
    _textView.editable = NO;
    _textView.selectable = NO;
    _textView.drawsBackground = NO;
    _textView.textContainer.lineFragmentPadding = 8.0;
    _textView.textContainerInset = NSMakeSize(4.0, 8.0);
    [_textView setFont:[NSFont systemFontOfSize:12.0]];
    [sv setDocumentView:_textView];
    [_window.contentView addSubview:sv];
  }
  return self;
}

- (void)showWithText:(NSString*)text {
  [_textView setString:text];
  [_textView.layoutManager ensureLayoutForTextContainer:_textView.textContainer];

  const CGFloat popup_width = 360.0;
  NSRect used = [_textView.layoutManager
      usedRectForTextContainer:_textView.textContainer];
  CGFloat height = ceil(used.size.height) + 20.0;
  height = MAX(height, 40.0);
  height = MIN(height, 500.0);

  NSPoint mouse = [NSEvent mouseLocation];
  NSScreen* screen = [NSScreen mainScreen];
  NSRect sf = screen.visibleFrame;

  CGFloat x = mouse.x + 25.0;
  CGFloat y = mouse.y - height / 2.0;
  if (x + popup_width + 20.0 > NSMaxX(sf))
    x = mouse.x - popup_width - 45.0;
  if (y < sf.origin.y)
    y = sf.origin.y;
  if (y + height > NSMaxY(sf))
    y = NSMaxY(sf) - height;

  NSRect wf = NSMakeRect(x, y, popup_width + 20.0, height);
  [_window setFrame:wf display:NO];
  [_textView setFrame:NSMakeRect(0, 0, popup_width + 20.0, height)];
  [_window orderFrontRegardless];
}

- (void)hide {
  [_window orderOut:nil];
}

@end

@interface MCPopupMenuDelegate : NSObject <NSMenuDelegate>
@property(nonatomic, strong) MCDescriptionPanel* descPanel;
@end

@implementation MCPopupMenuDelegate

- (instancetype)init {
  if (self = [super init]) {
    _descPanel = [[MCDescriptionPanel alloc] init];
  }
  return self;
}

- (void)menu:(NSMenu*)menu willHighlightItem:(nullable NSMenuItem*)item {
  if (!item) {
    [_descPanel hide];
    return;
  }
  MCMenuItemIdentifier* ident =
      (MCMenuItemIdentifier*)[item representedObject];
  NSString* desc = [ident itemDescription];
  if (!desc || [desc length] == 0) {
    [_descPanel hide];
    return;
  }
  [_descPanel showWithText:desc];
}

- (void)menuDidClose:(NSMenu*)menu {
  [_descPanel hide];
}

@end

@interface MCMenuBar : NSObject

@property(readonly) NSMenu* menuObject;
@property(nonatomic) void (*callback)(int16_t, int16_t);

@end

@implementation MCMenuBar

@synthesize callback;

- (id)initWithMenuListCallback:(const MenuList&)menuList callback:(void (*)(int16_t, int16_t))_callback {
  if (self = [super init]) {
    [self MCCreateMenu:menuList];
    callback = _callback;
  }
  return self;
}

- (IBAction)MCHandleMenuClick:(id)sender {
  id identifier = [sender representedObject];
  callback([identifier menuID], [identifier itemID]);
}

- (void)MCCreateMenu:(const MenuList&)menuList {
  _menuObject = [[NSMenu alloc] initWithTitle:@"Realmz"];
  [_menuObject setAutoenablesItems:NO];

  for (auto menu : menuList.menus) {
    NSString* title = [NSString stringWithCString:menu->title.c_str() encoding:NSMacOSRomanStringEncoding];
    NSMenuItem* menuItem = [[NSMenuItem alloc] initWithTitle:title action:NULL keyEquivalent:@""];
    menuItem.enabled = menu->enabled;
    [_menuObject addItem:menuItem];
    if (menu->items.size()) {
      NSMenu* subMenu = [self MCCreateSubMenu:title parentMenu:*menu submenus:menuList.submenus];
      [_menuObject setSubmenu:subMenu forItem:menuItem];
    }
  }
}

- (NSMenu*)MCCreateSubMenu:(NSString*)title parentMenu:(const Menu&)menu submenus:(const std::list<std::shared_ptr<Menu>>)submenus {
  NSMenu* newMenu = [[NSMenu alloc] initWithTitle:title];
  [newMenu setAutoenablesItems:NO];

  int itemId = 0;
  for (auto& subMenuItemRes : menu.items) {
    itemId++;
    if (subMenuItemRes.name == "-") {
      [newMenu addItem:[NSMenuItem separatorItem]];
    } else {
      NSString* name = [NSString stringWithCString:subMenuItemRes.name.c_str() encoding:NSMacOSRomanStringEncoding];
      if (name != nullptr) {
        char key_equiv[2] = {static_cast<char>(tolower(subMenuItemRes.key_equivalent)), '\0'};
        NSString* key = [NSString stringWithCString:key_equiv encoding:NSMacOSRomanStringEncoding];
        NSMenuItem* subMenuItem = [newMenu addItemWithTitle:name action:NULL keyEquivalent:key];
        [subMenuItem setTarget:self];
        [subMenuItem setAction:@selector(MCHandleMenuClick:)];
        id menuIdentifier = [[MCMenuItemIdentifier alloc] initWithRawIds:menu.menu_id itemId:itemId];
        [subMenuItem setRepresentedObject:menuIdentifier];
        subMenuItem.enabled = subMenuItemRes.enabled;
        if (subMenuItemRes.icon_image) {
          const auto& img = *subMenuItemRes.icon_image;
          auto png_bytes = img.serialize(phosg::ImageFormat::PNG);
          NSData* ns_data = [NSData dataWithBytes:png_bytes.data() length:png_bytes.size()];
          NSImage* icon = [[NSImage alloc] initWithData:ns_data];
          if (icon) {
            [icon setSize:NSMakeSize(16, 16)];
            [subMenuItem setImage:icon];
          }
        }
        if (subMenuItemRes.checked) {
          subMenuItem.state = NSControlStateValueOn;
        }

        // Submenu ids are specified by the itemMark field if the itemCmd field has
        // the value 0x1B
        // Macintosh Toolbox Essentials (3-96)
        if (subMenuItemRes.key_equivalent == 0x1B && subMenuItemRes.mark_character) {
          for (auto subMenuRes : submenus) {
            if (subMenuRes->menu_id == static_cast<uint8_t>(subMenuItemRes.mark_character)) {
              NSString* subMenuTitle = [NSString stringWithCString:subMenuRes->title.c_str() encoding:NSMacOSRomanStringEncoding];
              NSMenu* subMenu = [self MCCreateSubMenu:subMenuTitle parentMenu:*subMenuRes submenus:submenus];
              [newMenu setSubmenu:subMenu forItem:subMenuItem];

              break;
            }
          }
        }
      }
    }
  }

  return newMenu;
}

@end

@interface MCPopupMenu : NSObject

@property(readonly) NSMenu* contextualMenu;
@property(nonatomic) void (*callback)(int16_t, int16_t);
@property(nonatomic, strong) MCPopupMenuDelegate* menuDelegate;

@end

@implementation MCPopupMenu

@synthesize callback;

- (id)initWithWindow:(void *)nsWindow
    menu:(std::shared_ptr<Menu>)menu
    loc:(std::pair<int16_t, int16_t>)loc
    callback:(void (*)(int16_t, int16_t))_callback {
  if (self = [super init]) {
    callback = _callback;
    [self CreateMCPopupMenu:nsWindow menu:menu loc:loc];
  }
  return self;
}

- (IBAction)MCHandlePopupMenuClick:(id)sender {
  id identifier = [sender representedObject];
  callback([identifier menuID], [identifier itemID]);
}

- (void)CreateMCPopupMenu:(void *)nsWindow
    menu:(std::shared_ptr<Menu>)menu
    loc:(std::pair<int16_t, int16_t>)p {
  _contextualMenu = [[NSMenu alloc] initWithTitle:@"Contextual Menu"];
  [_contextualMenu setAutoenablesItems:NO];

  int itemId = 0;
  for(const auto& item: menu->items) {
    itemId++;
    NSString* name = [NSString stringWithCString:item.name.c_str() encoding:NSMacOSRomanStringEncoding];
    NSMenuItem *menuItem = [[NSMenuItem alloc] initWithTitle:name action:NULL keyEquivalent:@""];
    [menuItem setTarget:self];
    [menuItem setAction:@selector(MCHandlePopupMenuClick:)];
    NSString* desc = item.description.empty() ? nil
        : [NSString stringWithCString:item.description.c_str() encoding:NSUTF8StringEncoding];
    id menuIdentifier = [[MCMenuItemIdentifier alloc]
        initWithRawIds:menu->menu_id itemId:itemId description:desc];
    [menuItem setRepresentedObject:menuIdentifier];
    menuItem.enabled = item.enabled;
    if (item.checked) {
      menuItem.state = NSControlStateValueOn;
    }
    if (item.style_flags & 1) {
      NSFont* bold_font = [NSFont boldSystemFontOfSize:[NSFont systemFontSize]];
      NSDictionary* attrs = @{NSFontAttributeName: bold_font};
      NSAttributedString* bold_name = [[NSAttributedString alloc] initWithString:name attributes:attrs];
      [menuItem setAttributedTitle:bold_name];
    }
    [_contextualMenu addItem:menuItem];
  }

  _menuDelegate = [[MCPopupMenuDelegate alloc] init];
  [_contextualMenu setDelegate:_menuDelegate];

  // In the Cocoa framework, the origin is the bottom left. The point p is passed to us as (top, left).
  NSWindow* window = (NSWindow*)nsWindow;
  NSView* view = window.contentView;
  NSSize size = view.frame.size;
  NSPoint loc = NSMakePoint(p.second, size.height - p.first);

  [[NSNotificationCenter defaultCenter] addObserver:self
    selector:@selector(HandlePopupMenuClosed:)
    name:NSMenuDidEndTrackingNotification
    object:_contextualMenu];

  BOOL result = [_contextualMenu popUpMenuPositioningItem:nil atLocation:loc inView:view];
}

- (void)HandlePopupMenuClosed:(NSNotification*)notification {
  [[NSNotificationCenter defaultCenter] removeObserver:self
    name:NSMenuDidEndTrackingNotification
    object:_contextualMenu];
  callback(0, 0);
}

@end

void MCSync(std::shared_ptr<MenuList> menuList, void (*callback)(int16_t, int16_t)) {
  NSApplication* application = [NSApplication sharedApplication];

  static MCMenuBar* currentMenuBar = nil;
  currentMenuBar = [[MCMenuBar alloc] initWithMenuListCallback:*menuList callback:callback];

  application.mainMenu = [currentMenuBar menuObject];
}

void MCCreatePopupMenu(void *nsWindow, std::shared_ptr<Menu> menu, std::shared_ptr<MenuList> submenus, std::pair<int16_t, int16_t> loc, void (*callback)(int16_t, int16_t)) {
  [[MCPopupMenu alloc] initWithWindow:nsWindow menu:menu loc:loc callback:callback];
}
