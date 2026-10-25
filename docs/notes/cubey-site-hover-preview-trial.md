# Hover-preview trial — 2026-10-25

The first trial covers water, fire, ocean, clouds and the twelve-material lineup
in browsing cards. Scene players and the home hero expose native controls
directly. The previous extra play overlay has been removed from those players.

On a fine hover pointer, a 350 ms dwell over a card starts a separate silent
preview. Leaving the card restores its poster. Clicking the preview switches
to full native playback inline and keeps that player available thereafter.
Explicit playback blocks other hover previews. Preview playback stops offscreen,
when the page hides, on Escape/Tab, or when motion/data preferences change.
Devices without a fine hover pointer, reduced-motion users, supported data-saving
preferences and JavaScript-disabled browsers retain native controls.

All five excerpts are 640x360, 18 fps, 3.5 seconds, H.264/yuv420p, silent and
fast-start. They derive from the existing selected web films, with parent hashes
and exact encoding commands in
[the receipt](../../site/media/receipts/hover-previews.json).

| Scene | Preview bytes |
| --- | ---: |
| Water | 94,564 |
| Fire | 56,999 |
| Clouds | 67,096 |
| Materials | 77,091 |
| Ocean | 325,148 |
| Total | 620,898 |

The initial document contains preview URLs only as `data-src`; actual video
sources are assigned after hover intent. An already requested small clip may
finish buffering after departure and remains cached for reuse. No browser-side
transcoding or new native rendering is involved. Original full film identities
and imported-model notices remain unchanged.

The Node policy suite exercises deferred loading, incidental hover, exclusive
playback, caching, poster restoration, inline activation, manual-playback
priority, capability/data-saving fallback, offscreen/tab cancellation and a late
play promise after departure. Browser review separately confirms actual preview
decoding, transition between cards, native 1280x720 activation and manual playback
priority. Capability fallback tests use a simulated media/DOM harness rather
than claiming physical mobile-device acceptance.

## Hosting boundaries checked for this trial

GitHub's [Pages limits](https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits)
specify a 1 GB published-site maximum and soft 100 GB/month bandwidth limit.
The 10-builds/hour soft limit is exempted for custom Actions publishing, which
this site uses. The full built site is approximately 28.5 MB after this trial;
the largest full film remains about 6.7 MB.

The [regular repository limit](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github)
blocks individual files over 100 MiB and warns above 50 MiB. GitHub separately
documents that [Git LFS is not supported for Pages](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-git-large-file-storage).
These are distinct from README attachment uploads. Our 400 KB preview and 8 MB
full-film budgets are delivery choices, substantially below GitHub's limits.
Pages serves the pre-encoded files; it does not supply a media transcoding or
adaptive-quality service. The site can retain Pages while moving media to a
separate delivery host later if transfer volume or long/high-resolution films
justify it. No hosting change is required for this trial.
