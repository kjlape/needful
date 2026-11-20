// Needful on a phone. Everything the page shows comes from /api/state and is
// built with DOM calls (never innerHTML), so names and notes can't inject
// markup. Every visible string is in S, ready for translation.
"use strict";

const S = {
  views: { today: "Today", needful: "Needful", upcoming: "Upcoming", history: "History" },
  everything: "Everything",
  search: "Search",
  empty: {
    today: "Nothing pulled into today yet. Open a task in Needful and tap Today.",
    needful: "Nothing needs doing. Enjoy it.",
    upcoming: "Nothing coming up.",
    history: "Nothing done here yet.",
    search: "Nothing matches “%1”.",
  },
  doneToday: "Done today",
  noContext: "No context",
  done: "Done",
  markDone: "Mark done",
  today: "Today",
  offToday: "Off today",
  snooze: "Snooze",
  skip: "Skip this time",
  skipIt: "Skip",
  reason: "Why skip it this time? (optional)",
  skippedMark: "Skipped",
  markedSkipped: "Skipped: %1",
  wake: "Wake up",
  cancel: "Cancel",
  note: "Note for next time (optional)",
  comeBack: "When should it come back?",
  notAgain: "Not again",
  doneForGood: "This one is done for good.",
  comesBack: "It comes back on its schedule: %1.",
  alsoIn: "Also in",
  history: "History",
  notDoneYet: "Not done yet.",
  log: "Log",
  followUp: "Follow up",
  followUpDue: "Follow-up: %1",
  followedUp: "Followed up ✓",
  logNote: "Log a note",
  quickTask: "Quick task",
  whatHappened: "What did you notice?",
  context: "Context",
  followUpCheck: "Needs a follow-up",
  priority: "Priority",
  priorities: ["Low", "Normal", "High", "Urgent"],
  due: "Due",
  title: "What needs doing",
  save: "Save",
  add: "Add",
  logged: "Logged",
  added: "Added",
  markedDone: "Done: %1",
  snoozed: "Snoozed",
  offline: "Can't reach Needful. Is the server running?",
  failed: "That didn't work: %1",
  logUnavailable: "This server's Needful is too old to keep a log.",
  snoozeChoices: [["Tomorrow", 1], ["3 days", 3], ["1 week", 7], ["2 weeks", 14], ["1 month", 30], ["3 months", 91]],
  nextChoices: [["1 week", 7, "d"], ["2 weeks", 14, "d"], ["1 month", 1, "m"], ["3 months", 3, "m"], ["6 months", 6, "m"], ["1 year", 12, "m"]],
};

const POLL_MS = 20000;
const $ = (selector) => document.querySelector(selector);

// h("div", {class: "x", onclick: f}, child, "text") builds an element.
function h(tag, attrs, ...children) {
  const element = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs || {})) {
    if (value === null || value === undefined || value === false) continue;
    if (key.startsWith("on")) element.addEventListener(key.slice(2), value);
    else if (key === "class") element.className = value;
    else if (key === "text") element.textContent = value;
    else if (value === true) element.setAttribute(key, "");
    else element.setAttribute(key, value);
  }
  for (const child of children.flat()) {
    if (child === null || child === undefined || child === false) continue;
    element.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return element;
}

function remember(key, value) {
  try { localStorage.setItem("needful." + key, value); } catch (_) { /* private mode */ }
}
function recall(key, fallback) {
  try { return localStorage.getItem("needful." + key) ?? fallback; } catch (_) { return fallback; }
}

const app = {
  view: recall("view", "needful"),
  context: Number(recall("context", "0")) || 0,
  search: "",
  state: null,
  timer: null,
};

async function api(path, body) {
  const options = body === undefined ? {} : {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  };
  const response = await fetch(path, options);
  const data = await response.json().catch(() => ({}));
  if (!response.ok) {
    const error = new Error(data.error || response.statusText);
    error.status = response.status;
    throw error;
  }
  return data;
}

let toastTimer = null;
function toast(message) {
  const element = $("#toast");
  element.textContent = message;
  element.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { element.hidden = true; }, 2600);
}

async function refresh() {
  const query = new URLSearchParams({ view: app.view, context: String(app.context), search: app.search });
  try {
    app.state = await api("/api/state?" + query);
    $("#problem").hidden = !app.state.error;
    $("#problem").textContent = app.state.error || "";
  } catch (_) {
    $("#problem").hidden = false;
    $("#problem").textContent = S.offline;
    return;
  }
  render();
}

function applyTheme(state) {
  const root = document.documentElement.style;
  root.setProperty("--accent", state.accent);
  root.setProperty("--accent-fg", state.accentForeground === "white" ? "#fff" : "#000");
  // A dark accent fills buttons well but is hard to read as text on the
  // dark page, so text gets a lighter mix of it.
  root.setProperty("--accent-text", state.accentForeground === "white"
    ? "color-mix(in srgb, " + state.accent + " 45%, white)" : state.accent);
}

function render() {
  const state = app.state;
  applyTheme(state);
  // The server falls back to Everything when a remembered context is gone.
  app.context = state.context;

  $("#title").textContent = S.views[app.view];
  const current = state.contexts.find((c) => c.id === app.context);
  const where = current ? current.path : S.everything;
  $("#subtitle").textContent = app.view === "today" ? state.todayText + " · " + where : where;
  document.title = S.views[app.view] + " · Needful";

  const select = $("#context");
  select.replaceChildren(
    h("option", { value: "0", text: S.everything }),
    ...state.contexts.map((c) => h("option", {
      value: String(c.id),
      text: " ".repeat(c.depth) + c.name + (c.due ? "  (" + c.due + ")" : ""),
    })),
  );
  select.value = String(app.context);

  for (const tab of document.querySelectorAll(".tabs button")) {
    const view = tab.dataset.view;
    tab.querySelector(".tab-label").textContent = S.views[view];
    const count = view === "history" ? 0 : state.counts[view] || 0;
    tab.querySelector(".count").textContent = count ? String(count) : "";
    if (view === app.view) tab.setAttribute("aria-current", "page");
    else tab.removeAttribute("aria-current");
  }

  const list = $("#list");
  const rows = state.items.map((item) => item.kind === "task" ? taskRow(item) : historyRow(item));
  if (app.view === "today" && state.doneToday.length) {
    rows.push(h("p", { class: "section", text: S.doneToday }));
    rows.push(...state.doneToday.map((item) => historyRow(item, "done-today")));
  }
  if (!rows.length) {
    const text = app.search ? S.empty.search.replace("%1", app.search) : S.empty[app.view];
    rows.push(h("p", { class: "empty", text }));
  }
  list.replaceChildren(...rows);
}

function taskRow(task) {
  const late = task.overdue > 0;
  return h("button", { type: "button", class: "row", onclick: () => openTask(task.id) },
    h("span", { class: "mark p" + task.priority, "aria-hidden": "true" }),
    h("span", { class: "row-body" },
      h("span", { class: "row-title" }, task.title,
        task.today && app.view !== "today" ? h("span", { class: "badge", text: S.today }) : null),
      h("span", { class: "row-meta", text: [task.contextPath, task.lastDone].filter(Boolean).join(" · ") })),
    h("span", { class: "row-when" + (late ? (task.priority >= 4 ? " urgent" : " late") : ""), text: task.dueText }));
}

// Completions, and notes logged on a context.
function historyRow(item, extra) {
  const isLog = item.kind === "log";
  const text = isLog ? (item.text || item.note || "") : item.note;
  return h("div", { class: "row history " + (extra || "") + (isLog ? " log" : "") },
    h("span", { class: isLog ? "mark log" : item.skipped ? "mark done skipped" : "mark done", "aria-hidden": "true",
                text: isLog ? "✎" : item.skipped ? "↷" : "✓" }),
    h("span", { class: "row-body" },
      h("span", { class: "row-title" }, isLog ? S.log : item.title,
        item.skipped ? h("span", { class: "badge", text: S.skippedMark }) : null,
        isLog && item.task ? h("span", { class: "badge",
          text: item.followUpDone ? S.followedUp : S.followUpDue.replace("%1", item.followUpDueText || "") }) : null),
      item.contextPath ? h("span", { class: "row-meta", text: item.contextPath }) : null,
      text ? h("span", { class: "row-note", text }) : null),
    h("span", { class: "row-when", text: extra === "done-today" ? item.time : item.date }));
}

// A date n days or months from the server's today, as YYYY-MM-DD.
function after(amount, unit) {
  const [y, m, d] = app.state.today.split("-").map(Number);
  const date = unit === "m" ? new Date(y, m - 1 + amount, d) : new Date(y, m - 1, d + amount);
  const pad = (n) => String(n).padStart(2, "0");
  return date.getFullYear() + "-" + pad(date.getMonth() + 1) + "-" + pad(date.getDate());
}

function openSheet(sheet, ...content) {
  sheet.replaceChildren(h("div", { class: "grabber", "aria-hidden": "true" }), h("div", { class: "sheet-body" }, ...content));
  if (!sheet.open) sheet.showModal();
}

async function act(promise, message) {
  try {
    await promise;
    if (message) toast(message);
  } catch (error) {
    toast(S.failed.replace("%1", error.message));
  }
  await refresh();
}

async function openTask(id) {
  let task;
  try { task = await api("/api/task/" + id); } catch (error) { toast(S.failed.replace("%1", error.message)); return; }
  const sheet = $("#task-sheet");
  const close = () => sheet.close();

  const chips = h("div", { class: "chips" },
    [task.dueText, task.scheduleText, task.seasonText].filter(Boolean)
      .map((text, i) => h("span", { class: "chip" + (i === 0 && task.overdue > 0 ? " accent" : ""), text })));

  const also = (task.copies || []).length ? h("div", { class: "also" },
    h("div", {}, h("span", { class: "dim", text: S.alsoIn })),
    task.copies.map((c) => h("div", {}, h("span", { text: c.contextPath }), h("span", { text: c.dueText })))) : null;

  const snoozed = task.state === "snoozed";
  const actions = h("div", { class: "actions" },
    h("button", { type: "button", class: "btn primary", text: S.done, onclick: () => showDone(task) }),
    h("button", {
      type: "button", class: "btn", "aria-pressed": task.today ? "true" : "false",
      text: task.today ? S.offToday : S.today,
      onclick: () => { close(); act(api("/api/task/" + id + "/today", {})); },
    }),
    h("button", {
      type: "button", class: "btn", text: snoozed ? S.wake : S.snooze,
      onclick: () => snoozed ? (close(), act(api("/api/task/" + id + "/unsnooze", {}))) : showSnooze(task),
    }),
    h("button", { type: "button", class: "btn", text: S.skip, onclick: () => showDone(task, true) }));

  const history = h("div", { class: "history-list" }, h("h3", { text: S.history }),
    (task.history || []).length
      ? task.history.slice(0, 8).map((entry) => h("div", { class: "history-entry" },
          h("b", { text: entry.date }), " ", h("span", { class: "dim", text: entry.ago }),
          entry.skipped ? h("span", { class: "badge", text: S.skippedMark }) : null,
          entry.note ? h("div", { class: "notes", text: entry.note }) : null))
      : h("p", { class: "dim", text: S.notDoneYet }));

  openSheet(sheet,
    h("p", { class: "path", text: task.contextPath || S.noContext }),
    h("h2", { text: task.title }),
    chips,
    task.sharedNotes ? h("p", { class: "notes", text: task.sharedNotes }) : null,
    task.notes ? h("p", { class: "notes", text: task.notes }) : null,
    also,
    h("div", { id: "task-panel" }, actions),
    history);
}

function showSnooze(task) {
  const sheet = $("#task-sheet");
  $("#task-panel").replaceChildren(h("div", { class: "choices" },
    S.snoozeChoices.map(([label, days]) => h("button", {
      type: "button", class: "btn", text: label,
      onclick: () => { sheet.close(); act(api("/api/task/" + task.id + "/snooze", { days }), S.snoozed); },
    }))),
    h("button", { type: "button", class: "btn quiet", text: S.cancel, onclick: () => openTask(task.id) }));
}

// Done, or with skipping, passed over this time with a reason.
function showDone(task, skipping) {
  const sheet = $("#task-sheet");
  const asks = task.scheduleKind === "ask";
  const note = h("textarea", { name: "note", rows: "3", placeholder: skipping ? S.reason : S.note });
  let next = after(1, "m");
  const nextField = h("input", { type: "date", name: "next", value: next });
  nextField.addEventListener("change", () => { next = nextField.value; mark(); });
  const notAgain = h("input", { type: "checkbox", name: "notAgain" });
  const choiceButtons = S.nextChoices.map(([label, amount, unit]) => {
    const value = () => after(amount, unit);
    return h("button", {
      type: "button", class: "btn", text: label,
      onclick: () => { next = value(); nextField.value = next; notAgain.checked = false; mark(); },
      "data-value": value(),
    });
  });
  function mark() {
    for (const button of choiceButtons)
      button.setAttribute("aria-pressed", !notAgain.checked && button.dataset.value === next ? "true" : "false");
    nextField.disabled = notAgain.checked;
  }
  notAgain.addEventListener("change", mark);
  mark();

  const form = h("form", {
    onsubmit: (event) => {
      event.preventDefault();
      sheet.close();
      const nextDue = asks && !notAgain.checked ? next : "";
      if (skipping)
        act(api("/api/task/" + task.id + "/skip", { reason: note.value, next: nextDue }), S.markedSkipped.replace("%1", task.title));
      else
        act(api("/api/task/" + task.id + "/done", { note: note.value, next: nextDue }), S.markedDone.replace("%1", task.title));
    },
  },
  note,
  asks
    ? [h("label", {}, S.comeBack, h("div", { class: "choices" }, choiceButtons), nextField),
       h("label", { class: "check" }, notAgain, S.notAgain)]
    : h("p", { class: "dim", text: skipping ? task.skipText
                                   : task.scheduleKind === "once" ? S.doneForGood
                                   : S.comesBack.replace("%1", (task.scheduleText || "").toLowerCase()) }),
  h("div", { class: "actions" },
    h("button", { type: "submit", class: "btn primary", text: skipping ? S.skipIt : S.markDone }),
    h("button", { type: "button", class: "btn quiet", text: S.cancel, onclick: () => openTask(task.id) })));

  $("#task-panel").replaceChildren(form);
  note.focus();
}

function contextSelect(name) {
  const select = h("select", { name },
    h("option", { value: "0", text: S.noContext }),
    app.state.contexts.map((c) => h("option", { value: String(c.id), text: " ".repeat(c.depth) + c.name })));
  select.value = String(app.context);
  return select;
}

function prioritySelect() {
  const select = h("select", { name: "priority" },
    S.priorities.map((label, i) => h("option", { value: String(i + 1), text: label })));
  select.value = "2";
  return select;
}

function openAdd(tab) {
  const sheet = $("#add-sheet");
  const tabs = h("div", { class: "segmented", role: "tablist" },
    h("button", { type: "button", class: "btn", role: "tab", "aria-pressed": tab === "log" ? "true" : "false",
                  text: S.logNote, onclick: () => openAdd("log") }),
    h("button", { type: "button", class: "btn", role: "tab", "aria-pressed": tab === "task" ? "true" : "false",
                  text: S.quickTask, onclick: () => openAdd("task") }));
  openSheet(sheet, tabs, tab === "log" ? logForm(sheet) : taskForm(sheet));
}

function logForm(sheet) {
  const context = contextSelect("context");
  const text = h("textarea", { name: "text", rows: "4", required: true, placeholder: S.whatHappened });
  const followUp = h("input", { type: "checkbox", name: "followUp" });
  const priority = prioritySelect();
  const due = h("input", { type: "date", name: "due", value: app.state.today });
  const followFields = h("div", { class: "row-2", hidden: true },
    h("label", {}, S.priority, priority), h("label", {}, S.due, due));
  followUp.addEventListener("change", () => { followFields.hidden = !followUp.checked; });

  return h("form", {
    onsubmit: async (event) => {
      event.preventDefault();
      if (!text.value.trim()) return;
      const body = { context: Number(context.value), text: text.value, followUp: followUp.checked };
      if (followUp.checked) { body.priority = Number(priority.value); body.due = due.value; }
      try {
        await api("/api/log", body);
        sheet.close();
        toast(S.logged);
      } catch (error) {
        toast(error.status === 501 ? S.logUnavailable : S.failed.replace("%1", error.message));
        return;
      }
      refresh();
    },
  },
  h("label", {}, S.context, context),
  text,
  h("label", { class: "check" }, followUp, S.followUpCheck),
  followFields,
  h("div", { class: "actions" },
    h("button", { type: "submit", class: "btn primary", text: S.save }),
    h("button", { type: "button", class: "btn quiet", text: S.cancel, onclick: () => sheet.close() })));
}

function taskForm(sheet) {
  const title = h("input", { type: "text", name: "title", required: true, placeholder: S.title, enterkeyhint: "done" });
  const context = contextSelect("context");
  const priority = prioritySelect();
  const due = h("input", { type: "date", name: "due", value: app.state.today });
  return h("form", {
    onsubmit: (event) => {
      event.preventDefault();
      if (!title.value.trim()) return;
      sheet.close();
      const contexts = Number(context.value) ? [Number(context.value)] : [];
      act(api("/api/tasks", { title: title.value, contexts, priority: Number(priority.value), due: due.value }), S.added);
    },
  },
  title,
  h("label", {}, S.context, context),
  h("div", { class: "row-2" }, h("label", {}, S.priority, priority), h("label", {}, S.due, due)),
  h("div", { class: "actions" },
    h("button", { type: "submit", class: "btn primary", text: S.add }),
    h("button", { type: "button", class: "btn quiet", text: S.cancel, onclick: () => sheet.close() })));
}

// Tapping the dimmed area above a sheet closes it.
function closeOnBackdrop(sheet) {
  sheet.addEventListener("click", (event) => { if (event.target === sheet) sheet.close(); });
}

function poll() {
  clearInterval(app.timer);
  app.timer = document.visibilityState === "visible" ? setInterval(refresh, POLL_MS) : null;
}

function start() {
  $("#search").placeholder = S.search;
  for (const tab of document.querySelectorAll(".tabs button"))
    tab.addEventListener("click", () => {
      app.view = tab.dataset.view;
      remember("view", app.view);
      window.scrollTo(0, 0);
      refresh();
    });
  $("#context").addEventListener("change", (event) => {
    app.context = Number(event.target.value) || 0;
    remember("context", String(app.context));
    refresh();
  });
  let searchTimer = null;
  $("#search").addEventListener("input", (event) => {
    app.search = event.target.value;
    clearTimeout(searchTimer);
    searchTimer = setTimeout(refresh, 200);
  });
  $("#add").addEventListener("click", () => openAdd(recall("addTab", "log")));
  $("#add-sheet").addEventListener("click", (event) => {
    const tab = event.target.closest("[role=tab]");
    if (tab) remember("addTab", tab.textContent === S.logNote ? "log" : "task");
  });
  closeOnBackdrop($("#task-sheet"));
  closeOnBackdrop($("#add-sheet"));
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "visible") refresh();
    poll();
  });
  if ("serviceWorker" in navigator) navigator.serviceWorker.register("/sw.js").catch(() => {});
  refresh();
  poll();
}

start();
