# Gap analysis: People in Needful

People (`~/Work/people`) is a separate app for staying connected with
friends and the people a youth minister cares for. This compares it with
Needful to see what Needful would need before People's uses could live in
it instead.

## How People maps onto Needful today

Each person is a context (Friends › Maya Chen). Reaching out is a routine,
"Reach out", with a copy in each person's context, repeating from when it
was done. A copy keeps its own rhythm with "Only Maya".

| People | Needful today |
| --- | --- |
| Reach out every N days; Space logs it and resets the clock | The "Reach out" copy, done with a note |
| History of conversations | Completions with their notes, by context |
| Notes on what's happening in their life | Log notes on the person's context (`L`) |
| A follow-up from a conversation | A log note marked to follow up, which raises a one-time task |
| Remind me on a day | A one-time task due that day, in the person's context |
| Care list check-ins every N days | A repeating task |
| Sunday and midweek small group plans | A weekly fixed-rhythm task with notes on the coming time and a checklist; past times stay in history |
| City Week curriculum and camper paperwork | One-time tasks, or a checklist per camper (Registered, Paid, Forms) |
| City Week switching on January 1, 2027 | Its tasks snoozed until then |
| Search, undo and redo, one JSON file, the command line, translations | Already there |

### Not a gap: the private care list

People keeps its care list locked and out of the command line. Needful
already separates lists by universe, so the care list goes in a universe of
its own. That keeps it apart from everything else, though it can't make
one thing private inside a universe, and the universe still shows in the
window, the command line, and the phone page. That's enough for now.

## The gap: data fields on a context

A Needful context has only a name and a parent. People keeps details on
each person: phone, email, how I know them, birthday, anniversary. Rather
than build a "person" kind of context, contexts should get **data fields**
in general, so the same thing covers a person's birthday, a vehicle's VIN
and tire size, or a client's billing address.

A field has a label, a type, and a value. The types:

| Type | Examples | Shown as |
| --- | --- | --- |
| Text | How I know them, VIN | One line |
| Long text | Allergies, gate code and directions | Wrapped paragraph |
| Number | Odometer, tire pressure | Localized number, optional unit |
| Date | Birthday, anniversary, purchased | Formatted date; a date may leave out the year (a birthday without one) |
| Time | Usual call time, service hours | Localized time |
| Telephone | Mobile, home | Tappable `tel:` link |
| Email | Personal, work | Tappable `mailto:` link |
| Address | Home, office | Multi-line; opens in the maps app |
| URL | Website, account page | Tappable link |
| Yes/no | Registered, waiver signed | Checkbox |

What it needs:

- **Editing in the context dialog:** add, reorder, relabel, retype, and
  remove fields. Labels are free text, with the labels already used
  elsewhere offered first, so "Birthday" stays spelled one way.
- **A context detail pane** in the window and on the phone page, showing
  the fields above the context's history and notes, with "last done N days
  ago" for its tasks.
- **The command line:** `needful contexts set "Friends/Maya Chen" Birthday
  --date 03-14`, `contexts show` with `--json`, and `contexts unset`.
- **Search** matches field values, so searching "College roommate" finds
  Maya.
- **Sync:** fields merge like everything else. Different fields are all
  kept, and an edit to the same field on two machines goes to the later
  one. Removing a field is a tombstone, like a deleted task.
- **History stays put:** completions keep the context name they had;
  fields don't need to be kept that way.
- **Dates read leniently**, like People: `3/14`, `3/14/2025`, `2025-03-14`,
  `today`.

Fields hold information only. They don't change how needful anything is;
that's for the follow-ups below.

## Follow-ups

### Lead time

People brings birthdays and anniversaries up a week ahead. A yearly Needful
task ranks only once it's due, and shows earlier only in Upcoming. A task
should be able to show *N days before it's due*: it joins the needful list
early, as not yet late, so "Maya's birthday" comes up a week out and can be
done then. It pairs with date fields: a yearly task could take its date
from a context's Birthday field, show the age or count of years ("turns
30", "10th anniversary"), and roll the year over by itself.

### Tags

In People, one person can be in Friends and Ministry at once, with roles
(parent, small group, ministering, volunteer), and the parents' log is the
history of everyone tagged parent. Needful's contexts form a tree, so each
has exactly one parent, and filtering picks only one subtree. Tags on
contexts would let a context belong to several groups: filter the lists and
history by a tag beside the context tree, and search by tag.

## Nice to have

- **Drafting messages** (low priority). People writes a starting message,
  with versions for birthdays and anniversaries, and hands it to the
  `sms:`, `mailto:`, or `tel:` handler, or to the clipboard, and then logs
  it with one button. Built on telephone and email fields, this would be a
  "Message…" action on a task's context, which never sends anything itself.
- **Statuses beyond done.** City Week recruits go from asked to committed
  or declined. Roll-up counts over a context ("3 committed · 4 of 9 campers
  registered") would cover most of it.
- **Time of day on tasks**, for the City Week camp schedule (day, time,
  what). A long text field on the context may be enough.
- **Notes on later times**, not just the coming one, so you can plan two
  Sundays ahead.
- **The kind of contact** (text, call, visit, email) as quick picks in the
  done dialog, rather than in the note.
- **Done clears a reminder:** reaching out could answer a one-time reminder
  that has come due in the same context. This may fit the blocked tasks
  pitch.
- **The latest note on the row**, as People shows each person's, for
  contexts whose tasks have no checklist.
- **Ranking:** People puts the longest gap first, and Needful ranks by
  lateness in units of each task's own rhythm. They'll order a little
  differently; it's probably fine, but it's worth checking with real data.
