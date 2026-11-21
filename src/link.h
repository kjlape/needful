#pragma once

// The link library (link/, in Rust): encrypted connections between paired
// installs, addressed by key, and pairing by a one-time code. Commands and
// events are JSON; see link/src/lib.rs.
extern "C" {
struct NeedfulLink;
NeedfulLink *needful_link_start(const char *config, char **error);
int needful_link_event_fd(NeedfulLink *link);
char *needful_link_poll(NeedfulLink *link);
void needful_link_command(NeedfulLink *link, const char *command);
void needful_link_free_string(char *text);
void needful_link_stop(NeedfulLink *link);
char *needful_link_public_key(const char *secret);
char *needful_link_pair_code(NeedfulLink *link, const char *words);
char *needful_link_code_relay(const char *code);
}
