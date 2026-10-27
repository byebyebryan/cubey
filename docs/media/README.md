# Tracked media

The [presentation site](https://dev.byebyebryan.com/cubey/) owns the current
visual collection. Project guides should link to their current demo pages and
keep local figures when they explain a technical contract, diagnostic or
comparison. Historical captures remain useful evidence of what a particular
source revision produced.

## Ownership

| Collection | Owner and purpose |
| --- | --- |
| Current site captures | [Site media ledger and attribution](../../site/media/README.md), with selected files in `site/public/media/`. Use its import/preview workflow for new public media. |
| README banner | [Composition recipe](readme-hero.json) and [approved image](readme-hero.png). The [preparation tool](../../tools/README.md#readme-hero) reproduces the five-panel composition from pinned site captures. |
| Original README gallery | [Showcase package](showcase/README.md): earlier five-film publication, masters, recipes, attachment mappings and licensing. |
| Initial material stories | [Material presentation package](material-workshops/README.md): the original four-specimen PBR and wood-cut SDF stories, with their native masters and qualification inputs. |
| Other native evidence | The owning project, study or [evidence record](../evidence/README.md) retains its source, capture and qualification scope. Ignored raw outputs are not site-build inputs. |

Site selection and native capture are different identities. A newer web image
must not silently replace the evidence for an older result. Shared material
recipes also do not establish identical mesh/SDF optical transport, and saved
construction playback does not establish continuous brush editing.

## Retained publication packages

The two older packages above retain stable paths because project history,
reproduction tools, provenance records and media checks refer to them. Their
original recipes, hashes and source revisions describe those captures; running
today's native code establishes a new capture rather than renewing the old
qualification.

Some archived films are byte-identical to current site selections. Retention is
still a lifecycle decision: an archive stays pinned while the site can select a
later capture. Git stores identical files as one content blob, so removing a
duplicate path mainly reduces checkout clutter. Hash equality alone does not
show that a publication record is disposable.

New site media is self-hosted alongside the site. The older GitHub attachment
URLs remain archival delivery references; current project front doors should
link to the site. Their original issue/publication records and immutable reports
remain evidence, with no dependency on uploading the same media again.

## Reconciliation and retirement

Before consolidating or removing a file:

1. Trace its Markdown/site uses, manifest entries, parent/derivative references,
   reproduction/export tools and validation consumers. A provenance path can
   identify a historical input even when it is not a build dependency.
2. Select an owner with the right lifetime. Keep historical observations linked
   to their recorded media identity, rather than a mutable latest capture.
3. Update affected references, manifests and checks together, retaining a clear
   mapping to the earlier path, source revision and byte identity where needed.
4. Verify local links, media integrity, attribution and reproduction references.
   Verify the site when its selected assets or build inputs change.

Unique masters, recipes, qualification records and imported-asset notices are
not obsolete merely because their embeds leave a current README. In particular,
the original Damaged Helmet composite keeps the media-only `CC-BY-NC-4.0`
boundary documented in the [showcase package](showcase/README.md); it is absent
from the current selected site media. Preserve those notices with the retained
media. Git-history rewriting and deleting externally published attachments or
reports are separate storage/publication decisions.
