#ifndef NET_MOCK_H
#define NET_MOCK_H

// サーバとつなぐ前のダミーAPI
static inline void net_mock_join(void) {}
static inline void net_mock_phase_done(const char* phase) { (void)phase; }

#endif
