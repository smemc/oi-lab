# About oi-lab
Ubuntu extra configuration files and scripts for school labs in Mogi das Cruzes, SP, Brazil (including ProInfo multi-seat workstations)

# Our packages
* **oi-lab-freeze-users**: creates some users (`aluno0`, `aluno1`, `aluno2`, `aluno3`, `aluno4` and `freezetemplate`) and implements a "desktop freeze" mechanism for `aluno*` users, i.e., all changes made in current user session are automatically reverted after logout. One can customize `aluno*` desktop sessions by applying the desired changes in `freezetemplate` user session.

* **oi-lab-{x,l}ubuntu-extra-settings**: adds a context menu entry for Lubuntu/Xubuntu desktop sessions to open Terminal/File Manager/Text editor as root.

* **oi-lab-proinfo-multi-seat-utils**: provides automatic multi-seat support for ProInfo-MEC computers with Silicon Motion SM501 graphics cards. It currently supports the following computers:
  * pregão FNDE 83/2008 (up to 3 seats)
  * pregão FNDE 68/2009 - 2º lote (with a pair of SM501 graphics cards) (up to 5 seats)
  * pregão FNDE 71/2010 (up to 3 seats)
  
* **oi-lab-userful-rescue**: downloads a live ISO image with a minimalistic Ubuntu 12.04 system with Userful Multiseat 5.0 pre-enabled, used to work around a bug with some SM501 graphics cards, adds a GRUB menu entry for booting directly to this ISO image, and installs some scripts/systemd service units to automate boot to this ISO (it will automatically reboot back to your main system) when computer turns on.

# Recommended Ubuntu flavours for use with oi-lab packages
* For **single- or 2-seat** computers with **less than 1 GB of RAM**, **3-seat** computers with **less than 2 GB of RAM**, or **5-seat** computers with **less than 4 GB of RAM**, we recommend **Lubuntu 16.04.3 (32-bit architecture)**.
* For the other cases, you can choose freely between **Lubuntu, Xubuntu or Ubuntu MATE 16.04.3 or newer (64-bit architecture)**.

# How to install oi-lab packages in Ubuntu 16.04 and newer (all flavours[*])
1. Add our PPA to your repository lists:
```bash
sudo apt-add-repository ppa:oiteam/oi-lab
sudo apt update
```
2. Install desired packages:
```
sudo apt install oi-lab-freeze-users oi-lab-proinfo-multi-seat-utils oi-lab-userful-rescue (...)
```
3. **[UPDATE]** For new installations after November 17th, 2017, if you want to create users for desktop freezing, you must run manually the following command:
```
sudo oi-lab-create-freeze-users
```

[*] Package `oi-lab-proinfo-multi-seat-utils` may not work as expected in SDDM-based Ubuntu flavours (Kubuntu, KDE neon, Lubuntu-next), due to lack of multi-seat support in SDDM. In these cases, you must replace SDDM with LightDM.

# Additional recommendations
* When installing a new Ubuntu-based distro in a multi-seat ProInfo computer, you must partition your disk, creating a partition with size of **1 GB**, formatted with **ext2** filesystem, and with mount point **/boot**. This is needed to ensure that userful-rescue live ISO will boot correctly (it has proven to fail when the ISO is stored on a disk partition formatted with latest **ext4** filesystem).

# Enabling/Disabling userful-rescue

After installing `oi-lab-userful-rescue` package in computers with buggy SM501 video cards, you need to enable userful-rescue service manually. Just run
```bash
sudo userful-rescue-enable
```

After enabling userful-rescue boot scheduling service, your computer will be powered off automatically. When you turn it on again, it will automatically boot into userful-rescue live system, bringing your SM501 card back to normal, and then reboot back to your installed Ubuntu system.

If you want to disable userful-rescue boot scheduling service later, just run
```
sudo userful-rescue-disable
```
