<template>
  <nav class="breadcrumb" aria-label="Current view">
    <template v-if="reportWeek">
      <router-link :to="{ path: '/reports' }" class="crumb crumb-link">Reports</router-link>
      <span class="crumb-sep" aria-hidden="true">›</span>
      <span class="crumb crumb-tab">{{ reportWeek }}</span>
    </template>
    <span v-else-if="!deviceId" class="crumb crumb-tab">{{ tabLabel }}</span>
    <template v-for="key in DASH_FILTER_KEYS" :key="key">
      <button
        v-for="value in filtersStore[key]"
        :key="`${key}:${value}`"
        class="chip"
        :title="`Remove ${key} filter`"
        @click="filtersStore.remove(key, value)"
      >
        {{ key }} = {{ value }} <span class="chip-x" aria-hidden="true">&times;</span>
      </button>
    </template>
    <template v-if="deviceId">
      <router-link :to="{ path: '/devices', query: filtersStore.queryParams }" class="crumb crumb-link">Devices</router-link>
      <span class="crumb-sep" aria-hidden="true">›</span>
      <span class="crumb crumb-tab">{{ deviceId.slice(0, 8) }}…</span>
    </template>
  </nav>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import { useFiltersStore } from '@/stores/filters'
import { DASH_FILTER_KEYS } from '@/utils/urlState'

const TAB_LABELS: Record<string, string> = {
  health: 'Health',
  releases: 'Releases',
  stability: 'Stability',
  performance: 'Performance',
  fleet: 'Fleet',
  usage: 'Usage',
  prints: 'Prints',
  devices: 'Devices',
  reports: 'Reports'
}

const route = useRoute()
const filtersStore = useFiltersStore()

const routeName = computed(() => (typeof route.name === 'string' ? route.name : ''))
const tabLabel = computed(() => TAB_LABELS[routeName.value] ?? route.path)

// On a report page the trail ends in a link back to the report list, then the week.
const reportWeek = computed(() => (routeName.value === 'report' ? String(route.params.week ?? '') : ''))

// On a device page the trail ends in a link back to the filtered device list, then the device.
const deviceId = computed(() => (routeName.value === 'device' ? String(route.params.id ?? '') : ''))
</script>

<style scoped>
.breadcrumb {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 6px;
  font-size: 13px;
}

.crumb-tab {
  color: var(--text-primary);
  font-weight: 500;
  margin-right: 4px;
}

.crumb-link {
  color: var(--accent-blue);
  font-weight: 500;
  text-decoration: none;
}

.crumb-link:hover {
  text-decoration: underline;
}

.crumb-sep {
  color: var(--text-secondary);
}

.chip {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 2px 8px;
  background: transparent;
  border: 1px solid var(--border);
  border-radius: 10px;
  color: var(--text-secondary);
  font-size: 12px;
  cursor: pointer;
}

.chip:hover {
  color: var(--accent-red);
  border-color: var(--accent-red);
}
</style>
