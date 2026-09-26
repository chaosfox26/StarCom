# StarCom rules

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
