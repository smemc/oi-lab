#!/usr/bin/python3
# -*- coding: utf-8 -*-

import re
from sys import argv, stdout

import logging
from systemd.journal import JournalHandler

import pyudev

import xcffib
from xcffib import (NONE, randr)
from xcffib.xproto import (Atom, CW, ConfigWindow, PropMode, WindowClass)
import cairocffi

from time import (sleep, time)


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


class NodelessDevice:
    def __init__(self, device):
        self.device_path = device.device_path
        self.sys_path = device.sys_path
        self.sys_name = device.sys_name
        self.seat_name = device.get('ID_SEAT')


class Device(NodelessDevice):
    def __init__(self, device):
        super().__init__(device)
        self.device_node = device.device_node


class USBHubDevice(NodelessDevice):
    def __init__(self, device):
        super().__init__(device)
        self.product_id = device.attributes.asstring('idProduct')
        self.vendor_id = device.attributes.asstring('idVendor')


class InputDevice(Device):
    def __init__(self, device):
        super().__init__(device)
        self.parent = USBHubDevice(
            device.find_parent('usb', device_type='usb_device')
        )


class KMSVideoDevice(Device):
    def __init__(self, fb, drm):
        super().__init__(fb)
        self.drm = [Device(d) for d in drm]


class SM501VideoDevice(NodelessDevice):
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
        self.context.select_font_face('sans-serif')
        self.context.set_font_size(12)
        self.context.set_source_rgb(0, 0, 0)

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

    def write_message(self, message):
        lines = message.split('\n')
        y_all_extents = 0.

        # Set text position
        for line in lines:
            (x_bearing,
             y_bearing,
             text_width,
             text_height,
             *_) = self.context.text_extents(line)
            y_all_extents += (text_height/2 + y_bearing*2)

        y_all_extents = y_all_extents/2
        (x_bearing,
         y_bearing,
         text_width,
         text_height,
         *_) = self.context.text_extents(lines[0])
        x = self.width/2 - (text_width/2 + x_bearing)
        y = y_all_extents + self.height/2 - (text_height/2 + y_bearing)
        self.context.move_to(x, y)

        # Write text
        for line in lines:
            (x_bearing,
             y_bearing,
             text_width,
             text_height,
             *_) = self.context.text_extents(line)
            x = self.width/2 - (text_width/2 + x_bearing)

            self.context.move_to(x, y)
            self.context.show_text(line)

            y -= (text_height/2 + y_bearing*2)

        self.connection.flush()


def scan_keyboard_devices(context):
    devices = context.list_devices(subsystem='input', ID_INPUT_KEYBOARD=True)
    return [InputDevice(device) for device in devices if device.device_node]


def scan_mouse_devices(context):
    devices = context.list_devices(subsystem='input',
                                   ID_INPUT_MOUSE=True,
                                   sys_name='event*')
    return [InputDevice(device) for device in devices if device.device_node]


def scan_kms_video_devices(context):
    drms = context.list_devices(subsystem='drm')
    fbs = context.list_devices(subsystem='graphics')
    devices = [(fb, [drm for drm in drms
                     if drm.parent == fb.parent and drm.device_node])
               for fb in fbs if fb.device_node]
    return [KMSVideoDevice(*device) for device in devices]


def scan_sm501_video_devices(context):
    devices = context.list_devices(subsystem='platform', tag='master-of-seat')
    return [SM501VideoDevice(device) for device in devices]


def main():
    logger = logging.getLogger(argv[0])
    logger.setLevel(logging.INFO)
    logger.propagate = False
    stdout_handler = logging.StreamHandler(stdout)
    stdout_handler.setFormatter(logging.Formatter(
        '%(asctime)s %(name)s[%(process)s] %(levelname)s %(message)s'))
    logger.addHandler(stdout_handler)
    logger.addHandler(JournalHandler())

    context = pyudev.Context()
    keyboard_devices = scan_keyboard_devices(context)
    mouse_devices = scan_mouse_devices(context)
    kms_video_devices = scan_kms_video_devices(context)
    sm501_video_devices = scan_sm501_video_devices(context)

    for device in keyboard_devices:
        logger.info('Keyboard detected: %s -> %s',
                    device.device_node, device.sys_name)

    for device in mouse_devices:
        logger.info('Mouse detected: %s -> %s',
                    device.device_node, device.sys_name)

    for device in kms_video_devices:
        logger.info('KMS video detected: %s -> %s',
                    device.device_node, device.sys_name)

        for drm in device.drm:
            logger.info('>>> DRM node detected: %s -> %s',
                        drm.device_node, drm.sys_name)

    for device in sm501_video_devices:
        logger.info('SM501 video detected: %s', device.sys_name)

    windows = []

    for arg in argv[1:]:
        (display_name, *geometries) = arg.split(',')

        if len(geometries) == 0:
            geometries = [None]

        windows.extend([Window(xcffib.connect(display=display_name), g)
                        for g in geometries])

    for (i, w) in enumerate(windows):
        w.set_wm_name(f'w{i + 1}')
        w.write_message(f'Criada janela w{i + 1}.\nAguarde...')

    sleep(10)


if __name__ == '__main__':
    main()
