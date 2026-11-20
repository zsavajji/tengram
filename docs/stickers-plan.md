# Stickers: animated stickers and sending (plan, 2026-11-19, not started)

## Where things stand

- Engine: a sticker message is kind 5 in `chats.db`, with `extra.animated` (TGS and WebM alike) and `emoji`
  (`engine/messages.cpp`, `messageSticker`). `media.get` already downloads the sticker's own file, `.tgs` included
  (`message_file`).
- UI: `app/src/Sticker.cpp` decodes only the first WebP frame with libwebp, into a 180 px PNG
  (`cache/previews/<id>.png`). A TGS doesn't decode, so its bubble shows the "Sticker" label. `MediaBubble` shows the
  PNG in a plain `ImageView`.
- Sending: stickers can only be forwarded. There is no sticker list or picker.
- meegram2 (`../../meegram2/src/LottieAnimation.cpp`): a `QDeclarativeItem` that unzips the `.tgs` with zlib (gzip,
  `inflateInit2(15 + 16)`), renders each frame with `rlottie::renderSync` into a reused `QImage` and paints it. A
  message row plays the sticker once (`loop(1)`) and replays it on tap, behind an `animateStickers` setting. Its
  `rlottie/` folder is empty: rlottie came in as a prebuilt package, so we fetch the source ourselves.

## Two differences from meegram2

1. Cascades has no painter item. An animated sticker is a `QTimer` that renders a frame into a `bb::ImageData` and
   calls `ImageView::setImage`. `Waveform.cpp` and `QrImage.cpp` already build images this way, once rather than
   every frame.
2. The UI builds with the NDK's `qcc` (gcc 4.8), and rlottie needs C++14. Build rlottie with `bb10-clang`, as
   tgcalls and OpenSSL are, into a small `libtgs.so` that exports only rlottie's C API (`rlottie_capi.h`:
   `lottie_animation_from_data`, `lottie_animation_render`, …), with libc++ linked in statically and its symbols
   hidden. The UI calls it through plain C, so the two C++ runtimes never meet.
   Fallback: patch rlottie down to `-std=c++1y` and compile it inside `tengram.pro`, as libwebp is.

## Gates

### G1: does rlottie run on the Classic? (test binary only)
- `scripts/fetch-rlottie.sh` with a pinned commit, built in the docker flow (`build-calls.sh` is the model).
- `tests/tgs_check`: unzips a `.tgs` with the phone's zlib, then reports the frame count and the ms per frame at
  180 px. It runs from `/root/tengram`; nothing is installed.
- Pass: about 10 ms or less per frame on one core. Otherwise play at 15 fps instead of the file's 30/60, or turn
  on `-mfpu=neon`.

**G1 result (2026-11-20, on a Q10 SQN100-3; same Snapdragon S4 family as the Classic): passed, with changes.**
- `scripts/build-tgs.sh` builds `build/prefix/lib/libtgs.so` (~960 KB stripped, 16 exports, all `lottie_*`; needs only
  libc and libm) and `tests/tgs_check.c` both ways: qcc against the `.so`, and clang with rlottie static
  (`tgs_check_static`).
- **rlottie is Telegram Desktop's fork** (`desktop-app/rlottie` cbfdcdd), not Samsung's: Samsung's left parts of
  stickers out (`location.tgs` blank from frame 33, `blocked_peers_empty` 15 % covered against 41 %). The frames the
  fork drew were checked by eye (`palette`, `location`).
- pixman's NEON `.S` doesn't assemble with clang's assembler: it's preprocessed by clang and assembled by the armnto
  GNU `as` (`-meabi=5`). `-O3` made no difference over `-O2`.
- Six Telegram animations (512 px, 60 fps, 180 frames, 12 to 65 KB), per frame at 180 px: `sleep` 4.0 ms,
  `palette` 4.9, `cake` 6.2, `blocked_peers_empty` 9.1, `location` 9.8, `dog_empty` 17.1 (worst frame 34 ms, the
  first). At 128 px `dog_empty` is still 14.7 ms: the cost is geometry, not pixels, so smaller doesn't help.
  Unzipping takes 4 to 20 ms and parsing 20 to 60 ms, once per sticker.
- So: 12 to 30 % of a core at 30 fps for a typical sticker, about 50 % for the heaviest one.
- **Not proven yet:** the qcc binary loading `libtgs.so`. A program started over ssh can't map a library's code
  outside the system's trusted file system (`mmap` fails with EPERM even after `pathtrust !file`; `on -A
  all,allow,prot_exec` doesn't help). Installed bars load their own `native/lib/*.so`, which `pathtrust -t` also
  reports as untrusted, so the bar route should work. G2's package proves it: ship `libtgs.so` in `native/lib`. If
  it won't load, link rlottie and a static libc++ into the UI behind the same C calls.

### G2: stickers play in the chat
- New `StickerPlayer` (TGS through `libtgs`). **Frames render on a worker thread** (G1: 4 to 17 ms a frame would
  stall scrolling on the UI thread). The UI thread only turns the finished buffer into an `ImageData` and sets it on
  `MediaBubble::m_stickerImage`. A 60 fps sticker plays every other frame (30 fps), and parsing happens on the
  worker too (up to 60 ms). One sticker plays at a time: the newest shown or tapped.
- **Settings > Chats > Animated stickers (the user, 2026-11-20):** Loop (default, as stock Telegram), Play once
  (stops after one go; a tap plays it again, as stock Telegram with looping off), Off (still pictures, no
  animation at all).
- Playback stops when the card is recycled for another row, when the page is left, and when the app is minimized.
- No engine change: the UI sniffs the downloaded file (gzip magic = TGS), which also covers rows already in
  `chats.db`.
- `Sticker::picture` also makes the cached still PNG for a TGS (frame 0 through rlottie), for Off and for a
  sticker waiting its turn.
- WebM video stickers: skipped. There is no VP9 decoder, so they keep the static thumbnail. Worth a look only if
  the phone's OMX turns out to decode VP9. Animated WebP: skipped (Telegram's WebP stickers are still pictures).

### G3: the engine lists and sends stickers

| Request | TDLib | Answer |
|---|---|---|
| `stickers.sets` | `getInstalledStickerSets(stickerTypeRegular)` | `[{set, title, count, thumb}]` |
| `stickers.get` `{set}` (or `"recent"` / `"faves"`) | `getStickerSet` / `getRecentStickers` / `getFavoriteStickers` | `[{file, emoji, format, w, h, path?}]` |
| `stickers.file` `{file}` | `downloadFile`, quiet like `Sticker::fetch` | `{path}` |
| `sticker.send` `{chat, file, quote, topic}` | `sendMessage(inputMessageSticker(inputFileId))` | `{id}`, as `media.send` |
| `sticker.fave` `{file, on}` | `addFavoriteSticker` / `removeFavoriteSticker` | — |

- The grid uses each sticker's `thumbnail_` (128 px), not the full file, to keep downloads and memory small.
- Sets are fetched only when the picker opens, never preloaded: the engine's 30 MB cap, and the sticker-set reloads
  in NOTES.md (`docs/drafts/td-stickers-no-reload-on-order.patch`).
- TDLib file ids last only as long as the engine process, so the page asks for the list again after an engine
  restart.
- Check that TDLib adds a sent sticker to Recent on its own before writing anything for it.
- **Built (2026-11-20), `engine/stickers.cpp`, `stickers_handle`; compiles, not yet run on the phone.** Shapes in
  `docs/protocol.md` ("Stickers"). Deviations from the table: `stickers.sets` gives `cover` (a sticker object), not
  `thumb`; a sticker is `{file, emoji, format, pic, picPath?}` (no `w`, `h`: the grid is square cells; `pic` is the
  thumbnail's file id, or the sticker's own for a WebP or TGS without a still thumbnail, none for such a WebM; `picPath`
  when it is downloaded already); `stickers.file` does `getFile` first (400 or 404 is `not_found`), then
  `downloadFile` at priority 4. Sent stickers reach Recent on their own: TDLib's `on_sent_message_content` does it, so
  nothing was written for it.

### G4: the full-page sticker grid (`StickersPage.qml`)
- Opened from a Stickers cell at the end of `EmojiPicker`'s category strip, or a Stickers entry in the attach
  picker.
- A `ListView` with `GridListLayout`, 4 columns (about 180 px each at 720 px wide), headers for Recent,
  Favourites, then each installed set. Above it, a strip of set thumbnails that jumps to a set: the
  `EmojiPicker` pattern (`go`, `mark`).
- Cells are the static PNGs from `Sticker::picture`; nothing animates in the grid (a texture upload per cell per
  frame).
- **Decided (user, 2026-11-19): a tap sends straight away** (with the reply quote, if one is set) and closes the
  sticker page, back to the chat.
- A long press opens a preview overlay that shows the sticker large and still (nothing animates on the sticker page, the
  user, 2026-11-20), with Send and Add to / Remove from Favourites.
  Square by default, round with Appearance > Shape > Round, like the other new controls.
- New strings get translated, as usual.
- **Built (2026-11-20): `StickersPage.qml`, `StickerPicker.cpp`, `Sticker::gridPicture`; `scripts/build-ui.sh` compiles, not yet
  run on the phone.** Then moved to C++ (the user, 2026-11-20: slow, the grid black while scrolled): the page's content is
  `StickerGrid` (`StickerGrid.cpp`: strip, grid with a `ListItemProvider` as `MessageCards`, empty state, preview; the QML
  page is only the title bar and `signal picked`). `StickerPicker` is one for the app (`shared`): a page's `load()` keeps the
  groups and decoded pictures held and asks only Recent and Favourites again (a changed list replaces its group in place); a
  full load happens the first time and after the engine reconnects. Sets are asked up to four at once. Opened from a last cell of `EmojiPicker`'s strip (the Stickers entry in the attach picker is not
  done). Deviations from the plan above:
  - The grid's cells are 128 px PNGs of `Sticker::gridPicture` (cached as `st-<side>-<hash of path>.png`), decoded on a
    worker thread (`QtConcurrent`), at most 4 downloads or decodes at once, the last cell asked first; a cell asks for its
    picture when the grid reads its row. The sets are asked four at a time (the engine holds a list only while it answers).
  - `EmojiPicker`'s strip cells are 65 px wide, not 72: eleven fit the Q10's 720 px with the new cell. The sticker page's
    own strip scrolls sideways and keeps 72 px.
  - `StickerPicker.strip` carries a `key` beside `image`, so that the page sets a cell's picture only when it changed.
  - The failure of a request has no id: a failed `stickers.get` while a set is out skips that set; a `stickers.file`
    the engine never answers is given up after 20 s (`requestQuiet` reports no failure), and the cell asks again when read again.
  - `ChatPage.sendSticker` sets `followOwn`, as the other sends do, and shows `notSent` when the request did not go out.
  - The preview shows the sticker's own file still at 360 px (a TGS's frame 0). The emoji picker's Stickers button is fixed
    at the end of the category row, after a divider (the user, 2026-11-20).

## Out of scope for now
Searching stickers by emoji, adding or removing sets, custom emoji, WebM playback, animating the grid.

## How it runs
G1 is one test on the phone. Then G2 to G4 go into one package and one test round. G3 and G4 are written by a
`sonnet` subagent from this plan, and the diff is reviewed. G1 and the player are done in the main session
(debugging and design).
