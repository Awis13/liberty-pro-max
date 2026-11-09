# tvOS icon artwork

`AppIcon.png` is the unmodified 2000×1538 original PNG from
[Wikimedia Commons: GTA IV logo](https://commons.wikimedia.org/wiki/File:GTA_IV_logo.png),
downloaded at the user's request. Its SHA-1 is
`9304296d2f313a1a715f5a2cfab312fe6422ea0f`.

Original file: <https://upload.wikimedia.org/wikipedia/commons/5/5d/GTA_IV_logo.png>

The Commons file page identifies the logo as PD-textlogo (public domain
because it consists of text/simple shapes), with a separate trademark
notice. The artwork is not covered by the project's software MIT license.

`tools/make_tvos_icon.py` preserves the source alpha channel, scales the
logo inside the icon's safe area, and places it above an opaque black
background. The generated catalog and preview are in ignored `out/icons`.
