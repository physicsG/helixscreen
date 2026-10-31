# Installing HelixScreen on a Klipper Host (Raspberry Pi and Similar)

> **Status:** updating a Klipper host that already runs HelixScreen is the tested path today. A first install uses the same steps and is still being tested.

How the [HelixScreen easy installer](https://helixscreen.org/installer/) installs HelixScreen on the computer that runs Klipper: a Raspberry Pi, a BTT CB1 or CB2, an Orange Pi, a Radxa board, and the like. This is the path for Voron, RatRig and other printers whose Klipper host is a separate board with its own touchscreen. The command-line steps are in the [Installation guide](../INSTALL.md#generic-linux-install-raspberry-pi-btt-x86).

## What the Installer Needs

- A host running Klipper and Moonraker (MainsailOS, FluiddPi, KIAUH setups all qualify), switched on and on the same network as your computer
- SSH access to it, with a login that can use `sudo`
- A 64-bit (`aarch64`) or 32-bit (`armv7l`) ARM board. The installer picks the matching build for you
- A board the installer recognizes: Raspberry Pi, BigTreeTech CB1/CB2, Orange Pi, Radxa or Rock boards. It will not guess at a board it doesn't know; it stops and points you at the command-line guide

## Signing In

The installer opens an SSH session to the host and needs a login. There is no factory password on a Klipper host, so it always asks.

**With a password:** type the user name and password. You get three tries.

**With an SSH key:** if the installer finds a key on your computer, a "Use my SSH key instead of a password" box appears. It tries, in this order:

1. Keys held by your SSH agent (the OpenSSH agent on Windows, `ssh-agent` or your desktop's agent on macOS and Linux)
2. The key files in your `.ssh` folder, in the order `id_ed25519`, `id_ecdsa`, `id_rsa`

If a key file is protected by a passphrase, a "Key passphrase" field appears. Type the passphrase you chose when you made the key, or leave it empty if the key has none. A key held in your agent is already unlocked and needs no passphrase.

Keys with other file names aren't read. Add them to your agent first (`ssh-add ~/.ssh/my_key`) and the installer will use them.

The password and passphrase are used for this install only and are never saved to disk, to the logs or to the support bundle.

## The sudo Password

Installing needs root. If you sign in as `root`, or the account has passwordless sudo, nothing more is asked. Otherwise:

- Signed in with a **password**: the installer gives that password to sudo without asking again. If sudo rejects it (the account's sudo password differs from its login password), the install stops; sign in with an SSH key instead and the installer asks for the sudo password separately.
- Signed in with an **SSH key**: the installer has no password to give, so it asks for the account's password on a screen titled "Your printer needs your password". It checks the password with sudo before anything is uploaded and asks again if sudo refuses it, three tries in all.

If sudo can't be used at all, the installer tells you to sign in as root or give the account sudo rights. See the [Installation guide](../INSTALL.md#generic-linux-install-raspberry-pi-btt-x86) for the command-line route.

## Installing and Updating

On a host without HelixScreen the installer installs the newest stable release. The setup wizard then appears on the touchscreen.

On a host that already has HelixScreen it updates instead, and **"up to date" means:**

- It never installs an older version over a newer one, including a development build newer than any release.
- It follows the channel you are on. A stable install is offered stable releases. If a prerelease is installed, it is offered the newest of beta and stable, so a beta tester reaches the stable release once it is out.
- It reads the running `helix-screen` for the installed version. If it can't tell what is installed, it refuses to update and tells you to reinstall following the guide, because it could otherwise put an older release over a newer one.
- When nothing newer exists, it says HelixScreen is up to date and changes nothing.

## If It Stops

| What it says | What to do |
|--------------|------------|
| It doesn't recognize this computer's board | Use the [command-line install](../INSTALL.md#generic-linux-install-raspberry-pi-btt-x86) |
| There is no HelixScreen build for this CPU | Same: the command-line guide lists what is supported |
| HelixScreen doesn't appear on the screen | Give it five minutes, then check `sudo systemctl status helixscreen` and `journalctl -u helixscreen -n 100` on the host |

## See Also

- [Installation guide](../INSTALL.md): the command-line steps, display configuration and the setup wizard
- [Remote screen setup](../INSTALL.md#remote-screen-setup-run-on-a-separate-device): a screen device that drives a printer elsewhere
