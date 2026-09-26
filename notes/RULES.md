# StarCom rules

**Foundation (rule 6): no data collection. StarCom is completely decentralized and completely anonymous.**

1. Anonymity: the only name anywhere in StarCom is Chaos. No email addresses, no real names, no other identifying information.
2. Code is straight code: no comments, no fluff. Every explanation, design note or comment goes into a document in this notes folder.
3. The GitHub repository stays private until the owner says otherwise. No agent changes its visibility, and no one is invited, without her instruction.
4. Transparency: we are completely open about using AI to build StarCom. The AI-use statement is the very first thing in README.md and stays there.
5. Transparency is paramount. Users must always know exactly what StarCom uses and does. No hidden code and nothing malicious, ever. In practice:
   - Every library, service or external component StarCom uses is listed publicly in notes/, with what it is for.
   - Every network connection StarCom makes (to whom, when, why, what is sent) is documented in notes/.
   - Every piece of data StarCom stores or transmits (what, where, how long, encrypted or not) is documented in notes/.
   - No telemetry, tracking, analytics, hidden features, backdoors or obfuscated code. Nothing runs that is not in the published source.
   - Builds come from the published source only, so anyone can check that the program matches the code.
6. No data collection. This is the foundation of StarCom.
   - We collect nothing: no analytics, no usage data, no crash reports sent anywhere, no logs leaving the user's device.
   - No accounts: no login with email, phone number, Steam, Google, Discord or any other service. Nobody is ever asked who they are.
   - A user's identity is a key pair generated on their own device; the public key is their address. They choose any display name they like, and it is never tied to a real identity.
   - Completely decentralized: there is no central server that stores users, messages, contacts or metadata. Peers talk directly; anything that helps peers find each other must not store or learn who talks to whom.
   - Messages and calls are end-to-end encrypted between the people in them.
   - No age checks, ID checks, face scans or verification of any kind, because we never collect the data they would need.
7. Good neighbor protocol. StarCom is lightweight from the ground up: minimal CPU, memory, disk, battery and bandwidth use, and maximum efficiency on every kind of network (slow, metered, mobile, congested). It never floods a network, never hogs a machine, backs off when a link is busy, and stays quiet when idle.
