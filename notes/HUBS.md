# Hubs

A hub is a group space (SPEC section 6: Hubs > Channels > Chat). It has text channels and voice channels. Only people who are invited can join, and nobody outside a hub learns that it exists.

**The rule behind all of it holds here too:** no member ever receives another member's IP address. Everything between members goes through relays, encrypted end to end.

## What you can do
- **Create a hub.** You become its owner.
- **Invite people** (owner and admins). The hub's "..." menu makes an invite code that starts with `SCH1-` and ends with your @name. You send it privately, for example in a direct message.
  - The window's codes work for 24 hours and for up to 10 people.
  - The person who made the code has to be online while someone joins with it.
- **Join a hub** by pasting an invite code.
- **Talk in text channels.** Messages go to every member who is online. They are never stored: when you close StarCom they are gone, as with direct messages.
- **Join a voice channel.** Up to 8 people per voice channel. Use headphones: there is no echo cancellation yet.
- **Leave a hub** from its "..." menu. The owner cannot leave their own hub yet.

## Roles

| Role | Can |
|---|---|
| Owner (whoever created the hub) | Everything: rename the hub; add, rename and remove channels; make and remove admins; kick; ban; unban |
| Admin | Invite, kick and ban members (not other admins or the owner) |
| Member | Talk, join voice channels, see who is in the hub |

- There is one owner. The owner cannot be removed.
- Only the owner can change the hub's layout: its name, channels, roles and member list. So two admins can never make conflicting versions of it.
- Admins' joins (invites), kicks and bans are signed records. Every member checks them, and the owner's StarCom folds them into the next version of the hub.

## Limits (lightweight by design)
- At most 100 members per hub, and at most 24 channels.
- At most 8 people in one voice channel.
- A message holds up to 1,100 bytes. Each member can send 1 message per second, in bursts of up to 5, and every member enforces this.
- Presence ("online", "in voice channel X") is sent when it changes and every 5 minutes.

## How it works
- **One connection per pair of people.** When two members talk, whether in a direct message, in a hub, or in several hubs, it all shares one encrypted connection between the two of them, through relays. That connection closes after 5 minutes when nothing uses it.
- **A mesh, not a server.** Each member keeps encrypted connections to about 4 other members and passes hub messages along them. One of those links is swapped every 2 minutes, so the mesh repairs itself when people come and go. Neighbours also send each other a short version check (about 110 bytes) every 30 seconds, so a member who missed a change catches up.
- **Dandelion++.** A new message first travels along a random path of members ("stem") before it spreads to everyone ("fluff"). This follows the Dandelion++ design (Fanti et al., 2018): paths are chosen per 10-minute period, and every stem member spreads a message itself if it has not seen it spread within a few seconds.
- **Signed and encrypted.** Every message is encrypted with the channel's key and signed by its author. A member drops anything not signed by a current member, anything older than 10 minutes or seen before, and anything dated more than 60 seconds in the future.
- **Keys that move forward.** Each channel key is replaced every hour by a one-way hash of itself, and the old key is wiped. A key taken later from a device cannot unlock earlier traffic. When someone is kicked or banned, the hub switches to brand-new keys that the removed person never receives. Only the owner or an admin can start new keys: each new key carries their signature and a check value, so every member can verify it, and nobody else can slip in a key of their own. The owner or an admin also renews the keys about every 30 days, and whenever the person who made the current keys stops being an admin. Someone removed from a hub is sent the new layout once, so their StarCom knows and deletes the hub.
- **Invites.** The code holds the inviter's public key, a random token and the inviter's @name. Your StarCom finds the inviter by that @name, but only accepts the answer if it carries the same key as the code. So if someone else ever holds that @name, the token is never shown to them.
- **Names are checked.** A member's name in a hub is their @name, checked against their key in Star DNS before they are admitted. Nobody can join a hub under someone else's @name.

## Who learns what
- **Members** know who wrote each message: messages are signed, and names are shown. They also know who is online and who is in which voice channel. **They never learn anyone's IP address.**
- **Relays and mesh neighbours** cannot tell which member's connection a message started from: that is what Dandelion++ protects. Relays see only encrypted connections between keys, as for direct messages.
- **Nobody outside the hub** learns that it exists.

## Stored on your device
See DATA.md: the hub's signed layout, the signed kick, ban and join records, the channel keys (secret), and the invites you made (token hashes only). Messages are never stored.

## Known limits of this first version
- **If the owner's identity file is lost,** nobody can change the hub's channels or roles any more. The hub keeps working for talking.
- **If a member changes their @name,** the others cannot reach them by the old name until they join again with a new invite.
- **The inviter must be online** while someone joins. Later, any online admin will be able to accept.
- **The owner cannot leave or hand over their hub yet.**
- **Keys need an owner or admin now and then.** If no owner or admin opens StarCom for about 45 days, members cannot post until one of them does.
- **A voice channel and a direct call are not possible at the same time.** StarCom asks you to leave one first.
- **Direct messages from hub members.** A member who is not your friend can message you: it appears under MESSAGE REQUESTS as "in a hub with you". Accept adds them as a friend and opens the chat.
