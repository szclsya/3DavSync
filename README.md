# 3DavSync: Sync local folders with WebDAV
`3DavSync` allows you to sync folders on the 3DS SD card to a remote WebDAV server.

## Configuration
3DavSync accepts an ini-formatted config file at `/3ds/3DavSync.ini`. This file should look like this:

```ini
[General]
# List webdav configs that will be synced
Enabled=saves roms

# Example: Sync Checkpoint save folder with Nextcloud/ownCloud
[saves]
Url=https://example.org/remote.php/dav/files/username/saves
LocalPath=/3ds/Checkpoint/saves
# Specify credential here
Username=REDACTED
Password=REDACTED
Nextcloud=true

# Example: Sync roms, generic WebDAV server
[roms]
Url=https://example.org/whatever/webdav
LocalPath=/roms
Username=REDACTED
Password=REDACTED
```

## Build
To build, make sure `devkitARM` and common libraries are installed (usually just use the `3ds-dev` package group). Additionally, `3ds-curl` is needed.

Make sure the `devkitPro` environment variables are correctly configured. Then, just invoke `make`.
