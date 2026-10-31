<template>
  <div class="layout">
    <aside class="sidebar">
      <div class="sidebar-header">
        <h1>HelixScreen Analytics</h1>
      </div>
      <nav class="sidebar-nav">
        <router-link
          v-for="tab in TABS"
          :key="tab.path"
          :to="navTo(tab.path)"
          class="nav-link"
          :class="{ active: isActive(tab.path) }"
        >
          {{ tab.label }}
        </router-link>
      </nav>
      <div class="sidebar-footer">
        <button class="logout-btn" @click="handleLogout">Logout</button>
      </div>
    </aside>
    <main class="content">
      <div class="content-header">
        <FilterBar v-if="!route.meta.noFilters" />
        <router-link :to="navTo('/devices')" class="devices-link">Devices</router-link>
      </div>
      <div class="content-body">
        <Breadcrumb class="content-breadcrumb" />
        <slot />
      </div>
    </main>
  </div>
</template>

<script setup lang="ts">
import { watch } from 'vue'
import { useRoute } from 'vue-router'
import { useAuthStore } from '@/stores/auth'
import { useFiltersStore } from '@/stores/filters'
import { router } from '@/router'
import FilterBar from '@/components/FilterBar.vue'
import Breadcrumb from '@/components/Breadcrumb.vue'

// The Health tab is served at '/'.
const TABS = [
  { path: '/', label: 'Health' },
  { path: '/releases', label: 'Releases' },
  { path: '/stability', label: 'Stability' },
  { path: '/performance', label: 'Performance' },
  { path: '/fleet', label: 'Fleet' },
  { path: '/usage', label: 'Usage' },
  { path: '/prints', label: 'Prints' },
  { path: '/reports', label: 'Reports' }
]

const route = useRoute()
const auth = useAuthStore()
const filters = useFiltersStore()

// The URL is the source of truth: route changes (links, back/forward) feed the
// store, and store edits are written back with replace.
// A deliberate nav click carries the active filters; history entries stay as recorded.
// A tab with sub-pages (/reports/<week>) stays lit on them; '/' matches only itself.
function isActive(path: string): boolean {
  return route.path === path || (path !== '/' && route.path.startsWith(`${path}/`))
}

function navTo(path: string) {
  return { path, query: filters.queryParams }
}

watch(() => route.query, (q) => filters.syncFromRoute(q), { immediate: true })
watch(
  () => [filters.range, filters.platform, filters.version, filters.model, filters.board, filters.expand],
  () => filters.syncToRoute(router),
  { deep: true }
)

function handleLogout() {
  auth.logout()
  router.push('/login')
}
</script>

<style scoped>
.layout {
  display: flex;
  height: 100vh;
}

.sidebar {
  width: var(--sidebar-width);
  min-width: var(--sidebar-width);
  background: var(--bg-card);
  border-right: 1px solid var(--border);
  display: flex;
  flex-direction: column;
}

.sidebar-header {
  padding: 20px;
  border-bottom: 1px solid var(--border);
}

.sidebar-header h1 {
  font-size: 16px;
  font-weight: 600;
  color: var(--text-primary);
}

.sidebar-nav {
  flex: 1;
  padding: 12px 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.nav-link {
  display: block;
  padding: 10px 20px;
  color: var(--text-secondary);
  font-size: 14px;
  transition: color 0.15s, background 0.15s;
}

.nav-link:hover {
  color: var(--text-primary);
  background: rgba(59, 130, 246, 0.08);
}

.nav-link.active {
  color: var(--accent-blue);
  background: rgba(59, 130, 246, 0.12);
  font-weight: 500;
}

.sidebar-footer {
  padding: 16px 20px;
  border-top: 1px solid var(--border);
}

.logout-btn {
  width: 100%;
  padding: 8px;
  background: transparent;
  border: 1px solid var(--border);
  border-radius: 6px;
  color: var(--text-secondary);
  font-size: 13px;
  transition: color 0.15s, border-color 0.15s;
}

.logout-btn:hover {
  color: var(--accent-red);
  border-color: var(--accent-red);
}

.content {
  flex: 1;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
}

.content-header {
  padding: 16px 32px;
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
}

.devices-link {
  color: var(--accent-blue);
  font-size: 13px;
  white-space: nowrap;
}

.content-body {
  flex: 1;
  overflow-y: auto;
  padding: 32px;
}

.content-breadcrumb {
  margin-bottom: 16px;
}
</style>
