# Roadmap

Ideas that may come later, with what would decide them. None is promised.

## Pairing by short code from anywhere

*Tentative, pending demand.*

For now Needful uses n0's public relay by default, as a stopgap: it works
anywhere with nothing to set up, but it means relying on n0's servers, which
are meant for trying iroh out rather than for apps to lean on for good, and it
makes codes long. Codes carry the waiting computer's key and relay
(`7-violet-crumble-tango.<…>`), because the joiner can't look either up. With
no relay, a short code finds the waiting computer only on the same network, by
mDNS, or with `--at <host>`. A long code is fine to copy into a chat or a
note to yourself, and awkward to read out loud.

Getting a short code to work from anywhere needs some shared place where the
joiner can look up "who's waiting for code 7?" and get back a key and a relay.
The options:

- **A lookup service.** The waiting computer publishes its key and relay
  under a name made from the code, using iroh's DNS/pkarr address lookup,
  and the joiner looks the name up. This works out of the box with n0's
  public servers, but it means using an outside server, which Needful
  avoids. You could run your own pkarr server beside your relay instead.
- **A small rendezvous beside the relay.** A service next to the relay you
  already run keeps "nameplate → key" for a few minutes, much like
  magic-wormhole's mailbox server. It's one more thing to run, but it is
  yours.

Either way the code's words still prove both sides know it (SPAKE2), so a
lookup service only learns that some key is waiting for a nameplate.

The same choice decides the default relay: a relay of our own, or one each
person runs, would replace n0's.

**What would decide it:** people pairing across networks often, and finding
copying the long code a real nuisance, for example when the two computers
share no chat or clipboard; or n0's public relay becoming unreliable, or
limited, for Needful's traffic. Until then, the long code at the public relay
covers pairing from anywhere, and `--at` covers it without a relay.
