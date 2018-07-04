#!/usr/bin/python3
# -*- coding: utf-8 -*-

import re
import shutil
from sys import argv, stdout

# Logging modules
import logging
from systemd.journal import JournalHandler

# XCB window creation and drawing modules
import xcffib
import xcffib.randr
from xcffib.xproto import (Atom, CW, ConfigWindow, PropMode, WindowClass)
import cairocffi

# Input device handling modules
import asyncio
import pyudev
from evdev import (InputDevice, ecodes)

# DBus modules (for communication with systemd-logind)
from dbus import SystemBus

from time import (sleep, time)

MAX_SEAT_COUNT = 5
XORG_CONF_DIR = '/etc/X11/xorg.conf.d'
LOGIND_PATH = 'org.freedesktop.login1'
LOGIND_OBJECT = '/org/freedesktop/login1'
LOGIND_INTERFACE = 'org.freedesktop.login1.Manager'
SYSTEMD_PATH = 'org.freedesktop.systemd1'
SYSTEMD_OBJECT = '/org/freedesktop/systemd1'
SYSTEMD_INTERFACE = 'org.freedesktop.systemd1.Manager'

bus = SystemBus()
logind = bus.get_object(LOGIND_PATH, LOGIND_OBJECT)
systemd = bus.get_object(SYSTEMD_PATH, SYSTEMD_OBJECT)

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


def pci_format(pci_slot, delimiter=''):
    return re.sub(r'\.|:', delimiter, pci_slot)


def pci2display(pci_slot):
    return int(pci_format(pci_slot), base=16)


def parse_geometry(geometry):
    regex = '([0-9]+)x([0-9]+)(?:\\+([0-9]+)(?:\\+([0-9]+))?)?'
    match = re.match(regex, geometry).groups()
    return [int(x) if x else 0 for x in match] if match else None


def find_root_visual(screen):
    """Find the xproto.VISUALTYPE corresponding to the root visual"""
    for i in screen.allowed_depths:
        for v in i.visuals:
            if v.visual_id == screen.root_visual:
                return v


def update_file(file_path, new_data):
    try:
        with open(file_path, 'r') as read_file:
            old_data = read_file.read()

        if new_data != old_data:
            with open(file_path, 'w') as write_file:
                write_file.write(new_data)
    except FileNotFoundError:
        with open(file_path, 'w+') as new_file:
            new_file.write(new_data)


class Window:
    def __init__(self, display_number, geometry=None):
        logger.info('Connecting to X server :{}'.format(display_number))
        socket_unit = 'oi-lab-xorg-daemon@{}.socket'.format(display_number)
        systemd.StartUnit(socket_unit,
                          'replace',
                          dbus_interface=SYSTEMD_INTERFACE)
        self.connection = xcffib.connect(display=':{}'.format(display_number))
        self.id = self.connection.generate_id()

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
            xrandr = self.connection(xcffib.randr.key)
            screen_resources = xrandr.GetScreenResources(screen.root).reply()

            for output in screen_resources.outputs:
                # Get info from the output
                output_info = xrandr.GetOutputInfo(output, int(time())).reply()
                output_name = bytes(output_info.name).decode('ascii')

                if geometry == output_name:
                    # Output found!
                    crtc = output_info.crtc

                    if crtc != xcffib.NONE:
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

        # Set Cairo surface and context
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


class SeatNodelessDevice:
    def __init__(self, device):
        self.device_path = device.device_path
        self.sys_path = device.sys_path
        self.sys_name = device.sys_name
        self.pci_slot = device.find_parent(
            'pci').properties['PCI_SLOT_NAME'].lstrip('0000:')
        self.seat_name = device.get('ID_SEAT')

    def attach_to_seat(self, seat_name):
        try:
            logind.AttachDevice(seat_name, self.sys_path, False,
                                dbus_interface=LOGIND_INTERFACE)
            logger.info('Device %s successfully attached to seat %s',
                        self.sys_path, seat_name)
        except Exception as error:
            logger.error('Failed to attach device %s to seat %s!',
                         self.sys_path, seat_name)
            logger.error(error)


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
        def is_root_hub(device):
            # all root hubs have the same manufacturer 1d6b (Linux Foundation)
            return device.attributes.asstring('idVendor') == '1d6b'

        def get_parent_hub(device):
            parent = device.find_parent('usb', device_type='usb_device')
            return None if is_root_hub(parent) else (
                SeatHubDevice(parent) if 'seat' in parent.tags
                else get_parent_hub(parent)
            )

        super().__init__(device)

        # Only real USB hubs are allowed here!
        self.parent = get_parent_hub(device)

    def attach_to_seat(self, seat_name):
        if self.parent:
            # If input device is connected to a USB hub,
            # attach the hub to the seat instead, so that
            # all other devices connected to the same hub
            # will be automatically attached to the same seat.
            self.parent.attach_to_seat(seat_name)
        else:
            super().attach_to_seat(seat_name)


class SeatKMSVideoDevice(SeatDevice):
    def __init__(self, fb, drm):
        super().__init__(fb)
        display_number = pci2display(self.pci_slot)
        self.drm = [SeatDevice(d) for d in drm]
        self.window = Window(display_number)

    def attach_to_seat(self, seat_name):
        # Attach the framebuffer device node
        super().attach_to_seat(seat_name)

        for node in self.drm:
            # Attach all other DRM device nodes as well
            node.attach_to_seat(seat_name)


class SeatSM501VideoDevice(SeatNodelessDevice):
    def __init__(self, device):
        super().__init__(device)
        self.display_number = pci2display(self.pci_slot)
        self.output = device.get('SM501_OUTPUT')

        seat_address = pci_format(self.pci_slot, '-')
        xorg_address = pci_format(self.pci_slot, ':')
        config_file_path = '{}/21-oi-lab-sm501-{}.conf'.format(XORG_CONF_DIR,
                                                               seat_address)

        new_config_data = """Section "Device"
    MatchSeat "__fake-seat-{display_number}__"
    Identifier "Silicon Motion SM501 Video Card {pci_slot}"
    BusID "PCI:{xorg_address}"
    Driver "siliconmotion"
    Option "PanelSize" "1360x768"
    Option "Dualhead" "true"
    Option "monitor-LVDS" "Left Monitor"
    Option "monitor-VGA" "Right Monitor"
EndSection

Section "Screen"
    MatchSeat "__fake-seat-{display_number}__"
    Identifier "Silicon Motion SM501 Screen {pci_slot}"
    Device "Silicon Motion SM501 Video Card {pci_slot}"
    DefaultDepth 16
EndSection
""".format(display_number=self.display_number,
           pci_slot=self.pci_slot,
           xorg_address=xorg_address)
        update_file(config_file_path, new_config_data)

        # Enable permanently this socket unit, since it will be needed
        # even after multi-seat is configured.
        socket_unit = 'oi-lab-xorg-daemon@{}.socket'.format(
            self.display_number)
        systemd.EnableUnitFiles([socket_unit],
                                False, True,
                                dbus_interface=SYSTEMD_INTERFACE)

        self.window = Window(self.display_number, self.output)

    def attach_to_seat(self, seat_name):
        super().attach_to_seat(seat_name)

        config_file_path = '{}/22-oi-lab-nested-{}.conf'.format(XORG_CONF_DIR,
                                                                seat_name)

        new_config_data = """Section "Device"
    MatchSeat "{seat_name}"
    Identifier "Nested Device {pci_slot}"
    Driver "nested"
    Option "Display" ":{display_number}"
EndSection

Section "Screen"
    MatchSeat "{seat_name}"
    Identifier "Nested Screen {output} {pci_slot}"
    Device "Nested Device {pci_slot}"
    DefaultDepth 16
    Option "Output" "{output}"
EndSection
""".format(seat_name=self.seat_name,
           pci_slot=self.pci_slot,
           display_number=self.display_number,
           output=self.output)
        update_file(config_file_path, new_config_data)


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


def main():
    context = pyudev.Context()
    keyboard_devices = scan_keyboard_devices(context)
    mouse_devices = scan_mouse_devices(context)
    kms_video_devices = scan_kms_video_devices(context)
    sm501_video_devices = scan_sm501_video_devices(context)

    for device in keyboard_devices:
        logger.info('Keyboard detected: %s -> %s',
                    device.device_node, device.sys_path)

        if device.parent:
            logger.info('>>> Parent device: %s', device.parent.sys_path)

    for device in mouse_devices:
        logger.info('Mouse detected: %s -> %s',
                    device.device_node, device.sys_path)

        if device.parent:
            logger.info('>>> Parent device: %s', device.parent.sys_path)

    for device in kms_video_devices:
        logger.info('KMS video detected: %s -> %s',
                    device.device_node, device.sys_path)

        for drm in device.drm:
            logger.info('>>> DRM node detected: %s -> %s',
                        drm.device_node, drm.sys_path)

    for device in sm501_video_devices:
        logger.info('SM501 video detected: %s', device.sys_path)

    video_devices = kms_video_devices + sm501_video_devices

    # for arg in argv[1:]:
    #    (display_name, *geometries) = arg.split(',')

    #    if len(geometries) == 0:
    #        geometries = [None]

    #    windows.extend([Window(xcffib.connect(display=display_name), geometry)
    #                    for geometry in geometries])

    for (index, video_device) in enumerate(video_devices):
        video_device.window.set_wm_name('w{}'.format(index + 1))
        video_device.window.load_image('wait-loading.png')

    # The total number of configrable seats is limited by
    # the availability of video and keyboard devices.
    num_configurable_seats = min(MAX_SEAT_COUNT,
                                 len(video_devices) - 1,
                                 len(keyboard_devices) - 1)

    # Put this in a list, so it can be used globally in coroutines
    available_keyboards = [len(keyboard_devices)]

    # seat0 is already configured by default
    configured_seats = [True] \
        + [False]*num_configurable_seats \
        + [None]*(MAX_SEAT_COUNT - 1 - num_configurable_seats)

    def refresh_screens(loop):
        for (index, video_device) in enumerate(video_devices):
            status = ''.join(str(int(bool(is_configured)))
                             for is_configured in configured_seats[1:])
            remaining_seats = configured_seats.count(False)
            video_device.window.load_image(
                'seat{}-{}.png'.format(index, status))
            video_device.window.write_message(
                'Terminais restantes: {}        Teclados disponíveis: {}'
                .format(remaining_seats, available_keyboards[0])
            )

            if remaining_seats == 0:
                loop.stop()

    # EV_KEY event values: 0 (release), 1 (press), or 2 (hold)
    async def read_key(keyboard):
        device = InputDevice(keyboard.device_node)

        async for event in device.async_read_loop():
            # pylint: disable=no-member
            if event.type == ecodes.EV_KEY and event.value == 1:
                key = event.code - ecodes.KEY_F1 + 1
                return key

    async def read_all_keys(loop, keyboard):
        new_key_pressed = False
        refresh_screens(loop)

        while not new_key_pressed:
            key = await read_key(keyboard)
            new_key_pressed = (configured_seats[key] == False)

        if (key >= 1 and key <= 4):
            logger.info('Key F{} pressed on keyboard {}'
                        .format(key, keyboard.device_node))
            video_devices[key].attach_to_seat('seat-{}'.format(key))
            keyboard.attach_to_seat('seat-{}'.format(key))
            configured_seats[key] = True
            available_keyboards[0] -= 1
            refresh_screens(loop)

    sleep(1)

    if num_configurable_seats > 0:
        loop = asyncio.get_event_loop()
        coroutines = (read_all_keys(loop, keyboard)
                      for keyboard in keyboard_devices)
        future = asyncio.gather(*coroutines)

        try:
            loop.run_until_complete(future)
        except RuntimeError as error:
            if configured_seats.count(False) == 0:
                pass
            else:
                raise error

    logger.info('Multi-seat configuration finished.')
    sleep(1)


if __name__ == '__main__':
    main()
