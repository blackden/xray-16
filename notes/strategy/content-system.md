# Content System — OpenXRay macOS Fork

**Purpose**: Sustainable, integrated content production that doesn't steal engineering time; positions fork as serious, alive project.

**Owner**: Ragnar (solo dev + creator)
**Audience**: Mac gamers (primary), OpenXRay community (secondary), potential donors (tertiary)

---

## 1. Content Pillars (What to make)

### A. **Stable Release Showcase** (4–6 weeks, tied to `make promote`)
**Format**: YouTube short (90–120s) + Telegram thread
**Trigger**: Every `stable-YYYYMMDD` tag
**Production time**: 15–20 min
**Content**:
- 30s intro: "Here's what's new in this release"
- 45s gameplay clip (2–3 short scenes showing stability/polish)
- 30s callout (one visible win, e.g., "Cyrillic works now", "Updates in-game", "Force Quit gone")
- 15s CTA ("Join Telegram for updates")

**Template script**:
```
[INTRO CARD: Version tag + date]
"May 24 release: 4 weeks of fixes. Here's what changed."

[GAMEPLAY: 45s of clean play; no dead air]
- Start in safe, quiet zone (no distracting combat)
- Perform one visible action tied to the release
  (e.g., hit "Check for updates" button, save in Russian, quit gracefully)
- End with a smile / thumbs up / something natural

[CALLOUT CARD: One 1-2 sentence headline]
"Graceful quit — no more Force Quit"
or "Cyrillic saves work end-to-end"
or "Updates download in-game"

[CTA CARD: Telegram link]
"Questions? Telegram: t.me/xray_macos"
```

**Why this works**:
- Tied to engineering schedule (no "when should I film?" decision paralysis)
- 15–20 min production = no distraction from code
- YouTube Shorts algorithm loves frequent, consistent uploads
- One clip per feature = easy to repurpose to Telegram, Twitter, etc.

---

### B. **Deep Dives** (2–3 per quarter, non-blocking)
**Format**: YouTube long-form (8–15 min) + optional Twitch live breakdown
**Trigger**: After major feature lands (updater, UTF-8, safe-mode, renderer playground)
**Production time**: 60–90 min (includes script + basic editing + narration)
**Content**:
- Problem statement (what broke in vanilla)
- Before/after demo (vanilla vs fork)
- Brief technical explanation (why we fixed it + how it works)
- Playthrough showing the feature working in-game

**Example structure** (UTF-8 Deep Dive):
```
[INTRO] "S.T.A.L.K.E.R. on Mac — Russian text was broken. Here's how we fixed it."

[SEGMENT 1: THE PROBLEM] (2 min)
- Show vanilla fork
- Launch with Russian subtitles / Cyrillic NPC names
- Text appears as garbage
- Save game with Russian name → vanishes from menu
- Narrate: "This is what happens when an engine built for Windows in 2007 tries to handle UTF-8 on macOS."

[SEGMENT 2: WHAT WE DID] (3 min)
- Quick technical explainer (whiteboard sketch or animated diagram)
- cp1251 vs UTF-8 boundary
- Where the encoding breaks (file I/O, UI rendering, save serialization)
- Show the test suite (1500 lines of coverage)
- Narrate: "We rewrote three layers: file I/O, string validation, and the UI renderer."

[SEGMENT 3: THE RESULT] (2 min)
- Launch fork
- Same subtitles → perfect Cyrillic
- Save game with Russian name → appears in menu, loads correctly
- Play a scene with Russian NPC dialogue
- Narrate: "Now it's seamless. 100% Cyrillic support, end-to-end."

[OUTRO] "This is one of 8 major fixes in the fork. Join Telegram for updates."
```

**Why this works**:
- Establishes technical credibility (you *understand* the problem, not just "fixed it")
- 8–15 min fits YouTube algorithm for "watch time" metric
- Non-blocking (ship deep dives *after* engineering is done, not during)
- Repurposable: extract 60s clips for Shorts, clip highlights for Twitter/Telegram

---

### C. **Live Playthrough / Speedrun** (monthly, relaxed)
**Format**: YouTube Live (Twitch, mirror to YouTube) or pre-recorded speedrun (5–10 min)
**Trigger**: Whenever you feel like it (once per month, no pressure)
**Production time**: 5–10 min (just hit record, play, upload)
**Content**:
- You play through a sequence (ruins exploration, combat, base defense)
- Chat/viewers watch and ask questions
- You narrate casually (no script, just play the game)
- At end, demo one feature (update button, safe-mode recovery, etc.)

**Why this works**:
- Lowest production barrier (just play the game you already play)
- Builds parasocial connection (people see you, not a polished persona)
- Gives feedback loop ("which part was shaky? People clip it")
- Relaxed format = sustainable (not scripted burnout)

---

### D. **Bug Investigation Stream** (quarterly, educational)
**Format**: 30–60 min unscripted Twitch/YouTube Live
**Trigger**: When a user reports a reproducible bug or asks "why does this happen?"
**Production time**: 0 (just stream yourself debugging)
**Content**:
- User reports: "Graphics settings don't save" or "Game crashes on alt-tab"
- You open IDE, git log, profiler, reproduce the issue live
- Walk through the diagnosis process (console logs, test cases, git blame)
- Land on the fix or document the known limitation
- Ship a build that evening if it's quick; schedule issue if it's deeper

**Why this works**:
- Turns debugging time you'd do anyway into content
- Shows technical rigor (users trust you because you *investigate*, not guess)
- Educational for modding community
- Converts to blog posts / wiki articles afterward

---

## 2. Production Pipeline (How to make it)

### Hardware / Software
**Minimum setup** (what you probably have):
- Mac (M1, M3) running the fork
- Screen recording: macOS native (`Cmd+Shift+5`) or ScreenFlow (free tier)
- Audio: built-in mic or cheap USB headset (clarity > quality)
- Editing: iMovie (free) or DaVinci Resolve (free, better)

**Nice-to-have** (after 3 months if people engage):
- Dedicated test Mac mini (you mentioned this in roadmap) — runs fork on separate display
- Lavalier mic ($30) for less echo
- Ring light ($20) for live playthrough face cam (optional)

### Workflow

#### **Release Showcase (every 4–6 weeks)**
```
1. `make promote` lands stable build
2. Tag: `stable-YYYYMMDD-HHMMSS`
3. Boot build, test the release
4. Record 90s gameplay clip (3–5 takes, pick best)
5. Edit in iMovie: intro card + clip + callout card + CTA (15 min)
6. Upload to YouTube Shorts
7. Cross-post: clip to Telegram + Twitter
8. Write release notes for Gitea (reuse as video description)
```

**Time budget: 20 min top-to-bottom**

#### **Deep Dive (2–3 per quarter, after engineering done)**
```
1. Feature ships (e.g., #49 graceful Cmd+Q merged)
2. Wait 1 week (let it soak, make sure it's stable)
3. Write script outline (before/after, problem/solution)
4. Record vanilla fork failing (5 min)
5. Record your fork succeeding (5 min)
6. Record technical explainer segment (3 min; screenshare + narration)
7. Edit in DaVinci (extract clips, color grade, add captions)
8. Add music (free from YouTube Audio Library)
9. Upload to YouTube (8–15 min video)
10. Extract 60s clip for Shorts
```

**Time budget: 90 min for 12-min video (or 2 × 60 min across 2 evenings)**

#### **Live Playthrough (monthly)**
```
1. Open OBS or QuickTime
2. Start Twitch/YouTube Live
3. Play for 30–60 min (casual, no script)
4. Close stream
5. Auto-saves to YouTube
6. Next day: clip highlights, upload to Shorts
```

**Time budget: 0 prep, play time = time you'd spend anyway**

#### **Bug Investigation (when needed)**
```
1. User reports bug (Telegram/Gitea)
2. Reproduce locally
3. Go live: "Let's investigate X"
4. Debug for 30–60 min (live audience watches)
5. Document findings in issue
6. If fixed: tag commit, mention in next release notes
7. VOD auto-saves to YouTube; extract debugging tips for blog
```

**Time budget: 0 prep, stream time = work time**

---

## 3. Distribution Strategy (Where)

### **YouTube** (primary, algorithm-driven)
- **Shorts**: weekly release showcases (90–120s, Friday mornings)
- **Main channel**: deep dives (8–15 min, 2–3 per quarter; Tuesday)
- **Playlists**: organize by feature (UTF-8, Updater, Graceful Shutdown, etc.)

### **Telegram** (primary, engaged community)
- **Channel**: release notes + YouTube link on release day
- **Posts**: interesting bugs, user reports, community discussion
- **Pinned**: donation link (Ko-fi or Telegram donate button)
- **Vibe**: no ads, just updates + community chat

### **Twitter / X** (secondary, discoverability)
- Release clips (60s video)
- Bug fixes or engineering wins (text + screenshot)
- Weekly: "Here's what broke and how we fixed it"
- **Frequency**: 1–2 posts/week

### **Twitch / YouTube Live** (community engagement)
- Monthly playthrough (parasocial connection)
- Quarterly bug investigation (educational, builds trust)
- Auto-mirror to YouTube (one stream, two platforms)

### **Reddit, HN** (occasional, 3–4 posts/year)
- After major milestones (1.0 stable, UTF-8 done, Vulkan Phase 0)
- Not weekly; just significant announcements

---

## 4. Monetization Strategy

**Direction:** self-hosted payment intake через **Yookassa**, не сторонние
платформы. Reference implementation — `~/fedorov_tech/YouBooster` (бот
ragnar'а с уже интегрированной Yookassa). Полная мотивация в memory
`monetization-yookassa-self-hosted` и `notes/strategy/roadmap-4year.md`
section «Monetization vision».

### **Phase 1: Seed Community** (Months 1–3)
- **YouTube**: zero ad revenue expected yet
- **Telegram**: donation flow через self-hosted bot + Yookassa (не Patreon,
  не Boosty, не Ko-fi)
- **Goal**: 50–100 subs; пилотный поток донатов на ₽
- **Message**: «Поддержать — рублём через Yookassa, без посредников. На
  серверы, тестовый Mac mini, кофе ☕»

### **Phase 2: Sustainable revenue** (Months 6–12)
- YouTube Partner Program: при достижении eligibility — параллельный канал
- Telegram-bot donation flow: refined, обкатан, stable
- **Combined target**: устойчивый поток (конкретные цифры — после реальных
  данных Phase 1, не предсказывать в вакууме)

### **Phase 3: Sponsorships** (if traction > 10k followers)
- VPN/privacy tool sponsorships (fits audience)
- Game dev tool sponsorships
- **Rule**: only sponsor things you'd use; don't oversell

### **What NOT to do**:
- Don't paywall content (kills discovery; fork is free)
- Don't spam affiliate links (kills trust)
- Don't route through Boosty/Patreon/Ko-fi (комиссии посредникам, when
  ragnar has DevOps skills to host his own — wasteful)
- Don't gate Telegram content behind paywall tiers
- Keep monetization secondary to engineering

---

## 5. Content Calendar (Repeating)

### **Every 4–6 weeks** (tied to release)
- Monday: `make promote` ships stable
- Tue–Wed: record + edit release showcase
- Thursday: upload to YouTube Shorts + Telegram
- Friday: cross-post to Twitter, Reddit (if major feature)

### **Quarterly** (one per month)
- Week 1: pick feature for deep dive
- Week 2: record (60–90 min)
- Week 3: edit
- Week 4: upload YouTube main + extract Shorts

### **Monthly** (relaxed, no deadline)
- Friday evening: go live playthrough (30–60 min)
- Auto-records; clip highlights next day

### **As-needed** (organic)
- Bug report → if interesting → go live debug
- User question → might become video topic

---

## 6. Metrics & Feedback Loop

**Track monthly**:
```
YouTube Shorts:
  - Views per clip (avg, trend)
  - Click-through to YouTube main
  - Subscriber growth (goal: 10→50→200 over 6 months)

YouTube Main:
  - Watch time per video (goal: 50%+ retention)
  - Click-through to Telegram/GitHub (conversion)
  - Comments (engagement signal; answer them)

Telegram:
  - Monthly active users (goal: 20→100→500)
  - Donation patterns (growing? consistent?)
  - Questions asked (which features confuse users?)

Engagement:
  - Which topics get most views? (next deep dive idea)
  - Which features generate bugs? (quality signals)
  - Any mentions from journalists/influencers? (PR wins)
```

**Monthly review** (30 min):
- Pull analytics
- Check growth trends
- Identify top performer (most views)
- Identify most-discussed feature
- Decide: continue or pivot?

**Quarterly strategic check** (60 min):
- Can you fund Mac mini test rig yet? (If yes, buy it)
- Are viewers asking for features that align with roadmap? (Prioritize)
- Is content stealing engineering focus? (If yes, reduce frequency)

---

## 7. Content Roadmap (Next 12 Months)

### **Q2 2026** (June, NOW)
- [ ] Release showcase #1 (May stable)
- [ ] Deep dive #1 (Graceful Cmd+Q)
- [ ] Telegram channel launch
- [ ] YouTube channel seeded

### **Q3 2026** (July–September)
- [ ] Release showcase #2, #3, #4
- [ ] Deep dive #2 (UTF-8)
- [ ] Deep dive #3 (In-game updater)
- [ ] Monthly playthroughs (3×)
- [ ] Goal: 500 Telegram members, $100–200/month

### **Q4 2026** (Oct–Dec)
- [ ] Release showcase #5, #6
- [ ] Deep dive #4 (Vulkan Phase 1 or safe-mode)
- [ ] Holiday special (retrospective)
- [ ] Monthly playthroughs (3×)
- [ ] Goal: 2k YouTube followers, 1k Telegram, $200–500/month

### **2027**
- Content self-sustaining (15–20 min/week)
- Donor base funds Mac mini
- Fork has credibility + discoverable
- Onboard community creators (others do deep dives)

---

## 8. Why This Matters

**Your authentic story**: One person, one Mac, fixed a game that shouldn't work. 2 weeks of intensive engineering (272+ commits, 10-14h days). Now it just works.

**Why viewers care**:
- They own a Mac, want to play S.T.A.L.K.E.R., don't know it's possible
- They see you ship regularly and fix bugs they report
- Fork is alive, not abandoned; serious, not hobby

**Why donations happen**:
- You don't ask; you just ship good work
- Trust + small ask = 10% conversion
- 50 donors out of 500 engaged people = realistic

---

## Quick Start (This Week)

**Monday**:
- [ ] Create YouTube channel
- [ ] Create Telegram channel
- [ ] Draft release notes

**Tue–Wed**:
- [ ] Record 90s release showcase (use template above)
- [ ] Edit in iMovie

**Thursday**:
- [ ] Upload to YouTube Shorts
- [ ] Publish Telegram
- [ ] Pin donation message

**Friday**:
- [ ] Post to Twitter
- [ ] Share in OpenXRay community

**Result**: First video live by Friday. 30–40 views week 1 is normal.

---

**Status**: Initial draft
**Owner**: Ragnar
**Review**: June 30, 2026 (after first release showcase)
