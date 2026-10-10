# Demo script: Kith in 5 minutes

For showing Kith to anyone who keeps a shared list of people: a household, a club, a small team.
One laptop with Basecamp, one Android phone. You create a fresh book during the demo, so there is
nothing to prepare beyond installing. All names and numbers are made up.

## Before (10 minutes, once)

**Phone:** add the F-Droid repo from [apps.vpavlin.xyz](https://apps.vpavlin.xyz) and install
**Loam** and **Kith**. Kith syncs through Loam's shared node by default (the *Shared node: on*
button on the Books screen), so open Loam once and leave it running, then open Kith and approve
it in Loam when asked. The Loam banner at the top of Kith tells you if it isn't connected.

**Laptop:** Basecamp 0.3. Add the package repository `https://apps.vpavlin.xyz/logos-repo.json`
and install **Kith** (the `kith` core and `loam_core` come along). Kith signs with your Loam
identity; if you have several, it asks which one should own a new book.

Optional: have two or three contacts ready as vCard text (from any phone or mail app) to show
import.

## The demo

**1. A new book (laptop, 1 min).** Open Kith. On the left, under **Books**, click **+ New book**.
- Name it "Household". Under **Author as**, pick your identity (the default is preselected).
  Click **Create**.
- *Say:* "This identity owns the book and signs every change. No account, no sign-up: the key
  lives on this laptop."

**2. A contact (laptop, 1 min).** With the book selected, click **+ Add contact**.
- Fill **Display name** ("Marta Novak"), **Organization**, then **+ Add phone**, **+ Add email**,
  **+ Add address**. Click **Add**.
- Point at the list: the search box finds people by name, email, phone or handle.
- *Say:* "A proper address book: several phones, emails, messaging handles, addresses, notes."

**3. Share it to the phone (1 min).** Click **Share…** next to the book name.
- A QR code and a `kith://join…` link appear. On the phone: **Join book** → **Scan QR instead**,
  and point it at the screen. (Or copy the link and paste it into **Join book**.)
- "Household" opens on the phone and Marta appears within seconds.
- *Say:* "There's no server in between. The book is encrypted to the people who have this link,
  and the devices talk to each other directly."

**4. The aha: edit on one, see it on the other (1 min).**
- On the phone, tap **+ Add contact**, type "Dr. Svoboda (dentist)", **+ add phone**, tap **Save**.
  It shows up on the laptop.
- On the laptop, open Marta and add a note. Click **Save**. The phone has it too.
- Optional: put the phone in airplane mode, add a contact, turn it back on. It catches up on its
  own. *Say:* "Edits made offline aren't lost; they merge when the devices meet again."

**5. Bring your old contacts (phone or laptop, 30 s).** **Import vCard**, paste the vCard text,
**Import**. Going the other way: **Export book** (laptop) or **Export all** (phone); on the phone,
a long press on one contact exports just that person.
- *Say:* "Nothing is locked in. vCard in, vCard out."

**6. People you can verify (laptop, 30 s).** Open a contact and scroll to **🔑 Loam identity** →
**+ Add**. Paste their Loam address (0x…) and tick **Verified**.
- The contact gets a 🔑 in the list (the phone shows a small "loam" badge).
- *Say:* "Most contacts are just a name and a number. A contact with a Loam identity is someone
  other apps can recognise and invite, like a calendar or a budget shared with them."
- Adding the identity is laptop-only; the phone shows it read-only.

## If something goes wrong

- **The phone doesn't see the book or new contacts:** check Loam is running and Kith is approved
  there (Kith shows the Loam banner at the top if not). The status in Kith's top-right corner
  should not say *error*; it retries on its own and again when you reopen the app.
- **The laptop shows "Kith core is out of date":** update the `kith` package in Basecamp.
- **Create is greyed out:** the book needs a name.
- **Joining fails with "Invalid link":** the link must start with `kith://join`; copy it again
  with **Copy** in the share window.

## What to leave them with

- One address book on phone and laptop, shared with whoever you choose, no server, no account.
- Encrypted to the people in the book; every change signed by its author.
- Works offline; vCard in and out, so it fits next to what they already use.
- [apps.vpavlin.xyz](https://apps.vpavlin.xyz) to install.
