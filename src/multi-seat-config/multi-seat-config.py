#!/usr/bin/python3
# -*- coding: utf-8 -*-

import re
from sys import argv, stdout

import logging
from systemd.journal import JournalHandler

import asyncio
import pyudev
from evdev import (InputDevice, ecodes)

import xcffib
from xcffib import (NONE, randr)
from xcffib.xproto import (Atom, CW, ConfigWindow, PropMode, WindowClass)
import cairocffi

from time import (sleep, time)

logger = logging.getLogger(argv[0])
logger.setLevel(logging.INFO)
logger.propagate = False
stdout_handler = logging.StreamHandler(stdout)
stdout_handler.setFormatter(
    logging.Formatter(
        '%(asctime)s %(name)s[%(process)s] %(levelname)s %(message)s'
    )
)
logger.addHandler(stdout_handler)
logger.addHandler(JournalHandler())


def parse_geometry(geometry):
    regex = '([0-9]+)x([0-9]+)(?:\\+([0-9]+)(?:\\+([0-9]+))?)?'
    match = re.match(regex, geometry).groups()
    return [int(x) if x else 0 for x in match] if match is not None else None


def find_root_visual(screen):
    """Find the xproto.VISUALTYPE corresponding to the root visual"""
    for i in screen.allowed_depths:
        for v in i.visuals:
            if v.visual_id == screen.root_visual:
                return v


class SeatNodelessDevice:
    def __init__(self, device):
        self.device_path = device.device_path
        self.sys_path = device.sys_path
        self.sys_name = device.sys_name
        self.seat_name = device.get('ID_SEAT')


class SeatDevice(SeatNodelessDevice):
    def __init__(self, device):
        super().__init__(device)
        self.device_node = device.device_node


class SeatHubDevice(SeatNodelessDevice):
    def __init__(self, device):
        super().__init__(device)
        self.product_id = device.attributes.asstring('idProduct')
        self.vendor_id = device.attributes.asstring('idVendor')


class SeatInputDevice(SeatDevice):
    def __init__(self, device):
        def get_parent_hub(device):
            parent = device.find_parent('usb', device_type='usb_device')
            return None if parent is None else (
                parent if 'seat' in parent.tags else get_parent_hub(parent))

        super().__init__(device)
        self.parent = SeatHubDevice(get_parent_hub(device))


class SeatKMSVideoDevice(SeatDevice):
    def __init__(self, fb, drm):
        super().__init__(fb)
        self.drm = [SeatDevice(d) for d in drm]


class SeatSM501VideoDevice(SeatNodelessDevice):
    def __init__(self, device):
        super().__init__(device)
        self.output = device.device.get('SM501_OUTPUT')


class Window:
    def __init__(self, connection, geometry=None):
        self.connection = connection
        self.id = connection.generate_id()

        screen = self.connection.get_setup().roots[self.connection.pref_screen]

        # Try to get window geometry explictly
        try:
            (self.width, self.height, self.x, self.y) = parse_geometry(geometry)
        except:
            # Initialize window geometry as full screen size
            self.x = 0
            self.y = 0
            self.width = screen.width_in_pixels
            self.height = screen.height_in_pixels

            # Get window geometry from RandR output name
            xrandr = connection(randr.key)
            screen_resources = xrandr.GetScreenResources(screen.root).reply()

            for output in screen_resources.outputs:
                # Get info from the output
                output_info = xrandr.GetOutputInfo(output, int(time())).reply()
                output_name = bytes(output_info.name).decode('ascii')

                if geometry == output_name:
                    # Output found!
                    crtc = output_info.crtc

                    if crtc != NONE:
                        # Output is enabled! Get its CRTC geometry
                        crtc_info = xrandr.GetCrtcInfo(
                            crtc, int(time())
                        ).reply()
                        self.x = crtc_info.x
                        self.y = crtc_info.y
                        self.width = crtc_info.width
                        self.height = crtc_info.height
                        break

        # Create window
        self.connection.core.CreateWindow(xcffib.CopyFromParent,
                                          self.id,
                                          screen.root,
                                          self.x, self.y,
                                          self.width, self.height,
                                          0,
                                          WindowClass.InputOutput,
                                          screen.root_visual,
                                          CW.BackPixel,
                                          [screen.white_pixel])

        # Show window
        self.connection.core.MapWindow(self.id)

        # Force window placement after MapWindow() call,
        # in order to prevent window position
        # from being eventually overriden by WM.
        # self.connection.core.ConfigureWindow(self.id,
        #                                     ConfigWindow.X | ConfigWindow.Y,
        #                                     [self.x, self.y])

        # Set Cairo surface with given text font
        self.context = cairocffi.Context(
            cairocffi.XCBSurface(self.connection,
                                 self.id,
                                 find_root_visual(screen),
                                 self.width,
                                 self.height)
        )

        self.connection.flush()

    def set_wm_name(self, name):
        self.name = name
        self.connection.core.ChangeProperty(PropMode.Replace,
                                            self.id,
                                            Atom.WM_NAME,
                                            Atom.STRING,
                                            8,
                                            len(name),
                                            name)

    def load_image(self, image_path):
        self.connection.core.ClearArea(False, self.id, 0, 0, 0, 0)

        image_surface = cairocffi.ImageSurface.create_from_png(image_path)
        image_width = image_surface.get_width()
        image_height = image_surface.get_height()

        self.context.set_source_rgb(0, 136/255, 170/255)
        self.context.paint()

        self.context.set_source_surface(image_surface,
                                        (self.width-image_width) / 2,
                                        (self.height-image_height) / 2)
        self.context.paint()

        self.connection.flush()

    def write_message(self, message):
        self.context.select_font_face('sans-serif')
        self.context.set_font_size(24)
        self.context.set_source_rgb(1, 1, 1)
        self.context.move_to(10, 30)
        self.context.show_text(message)

        self.connection.flush()


def scan_keyboard_devices(context):
    devices = context.list_devices(subsystem='input', ID_INPUT_KEYBOARD=True)
    return [SeatInputDevice(device) for device in devices if device.device_node]


def scan_mouse_devices(context):
    devices = context.list_devices(subsystem='input',
                                   ID_INPUT_MOUSE=True,
                                   sys_name='event*')
    return [SeatInputDevice(device) for device in devices if device.device_node]


def scan_kms_video_devices(context):
    drms = context.list_devices(subsystem='drm')
    fbs = context.list_devices(subsystem='graphics')
    devices = [(fb, [drm for drm in drms
                     if drm.parent == fb.parent and drm.device_node])
               for fb in fbs if fb.device_node]
    return [SeatKMSVideoDevice(*device) for device in devices]


def scan_sm501_video_devices(context):
    devices = context.list_devices(subsystem='platform', tag='master-of-seat')
    return [SeatSM501VideoDevice(device) for device in devices]


async def read_key(device):
    async for event in device.async_read_loop():
        # Reminder: EV_KEY event values: 0 (release), 1 (press), or 2 (hold)
        # pylint: disable=no-member
        if event.type == ecodes.EV_KEY and event.value == 1:
            key = event.code - ecodes.KEY_F2 + 2
            return key

pressed_keys = [False, False, True, True]


async def read_all_keys(windows, device):
    def refresh_screens(windows, pressed_keys):
        for (index, window) in enumerate(windows):
            window.load_image(
                'seat{}-{}.png'.format(index + 1,
                                       ''.join(str(int(is_pressed))
                                               for is_pressed in pressed_keys)
                                       )
            )
            window.write_message(
                'Teclados disponíveis: {}'.format(pressed_keys.count(False))
            )

    new_key_pressed = False
    refresh_screens(windows, pressed_keys)

    while not new_key_pressed:
        pressed_key = await read_key(device)
        new_key_pressed = not pressed_keys[pressed_key - 1]

    logger.info(
        'Key F{} pressed on keyboard {}'.format(pressed_key, device.fn)
    )

    if (pressed_key == 1 or pressed_key == 2):
        pressed_keys[pressed_key - 1] = True
        refresh_screens(windows, pressed_keys)


def main():
    context = pyudev.Context()
    keyboard_devices = scan_keyboard_devices(context)
    mouse_devices = scan_mouse_devices(context)
    kms_video_devices = scan_kms_video_devices(context)
    sm501_video_devices = scan_sm501_video_devices(context)

    for device in keyboard_devices:
        logger.info('Keyboard detected: %s -> %s',
                    device.device_node, device.sys_path)
        logger.info('>>> Parent device: %s', device.parent.sys_path)

    for device in mouse_devices:
        logger.info('Mouse detected: %s -> %s',
                    device.device_node, device.sys_path)
        logger.info('>>> Parent device: %s', device.parent.sys_path)

    for device in kms_video_devices:
        logger.info('KMS video detected: %s -> %s',
                    device.device_node, device.sys_path)

        for drm in device.drm:
            logger.info('>>> DRM node detected: %s -> %s',
                        drm.device_node, drm.sys_path)

    for device in sm501_video_devices:
        logger.info('SM501 video detected: %s', device.sys_path)

    windows = []

    for arg in argv[1:]:
        (display_name, *geometries) = arg.split(',')

        if len(geometries) == 0:
            geometries = [None]

        windows.extend([Window(xcffib.connect(display=display_name), geometry)
                        for geometry in geometries])

    for (index, window) in enumerate(windows):
        window.set_wm_name('w{}'.format(index + 1))
        window.load_image('wait-loading.png')

    sleep(1)
    keybds = [InputDevice(device.device_node) for device in keyboard_devices]
    loop = asyncio.get_event_loop()
    loop.run_until_complete(asyncio.gather(
        *(read_all_keys(windows, keybd) for keybd in keybds)))
    sleep(1)


if __name__ == '__main__':
    main()
