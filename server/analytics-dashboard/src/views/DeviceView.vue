<template>
  <AppLayout>
    <div class="page">
      <div class="page-header">
        <h2>Device <span class="mono">{{ shortDeviceId(id) }}</span></h2>
      </div>

      <DashSection title="Device" :loading="loading" :error="error">
        <div v-if="missing" class="empty-state">No such device in the last 90 days</div>
        <template v-else-if="data">
          <div class="chart-section">
            <h3>{{ data.model || 'Unknown model' }}</h3>
            <ChartCaption :text="`${data.platform || '-'} / ${data.board || '-'} / ${data.version || '-'}; first seen ${formatTimestamp(data.first_seen)}, last seen ${formatTimestamp(data.last_seen)}`" />
            <p class="mono device-id">{{ data.device_id }}</p>
          </div>

          <div class="grid-2col">
            <div class="chart-section">
              <h3>Hardware</h3>
              <ChartCaption text="Latest hardware report" />
              <div v-if="isEmpty(data.hardware)" class="empty-state">No hardware report</div>
              <div v-else class="table-wrapper">
                <table class="data-table">
                  <tbody>
                    <tr v-for="(v, k) in data.hardware!" :key="k">
                      <td>{{ titleCase(String(k)) }}</td>
                      <td class="mono">{{ v }}</td>
                    </tr>
                  </tbody>
                </table>
              </div>
            </div>
            <div class="chart-section">
              <h3>Settings</h3>
              <ChartCaption text="Latest settings report" />
              <div v-if="isEmpty(data.settings)" class="empty-state">No settings report</div>
              <div v-else class="table-wrapper">
                <table class="data-table">
                  <tbody>
                    <tr v-for="(v, k) in data.settings!" :key="k">
                      <td>{{ titleCase(String(k)) }}</td>
                      <td class="mono">{{ v }}</td>
                    </tr>
                  </tbody>
                </table>
              </div>
            </div>
          </div>

          <div class="chart-section">
            <h3>Version History</h3>
            <ChartCaption text="Releases this device has reported, newest first" />
            <div v-if="data.versions.length === 0" class="empty-state">No version history</div>
            <div v-else class="table-wrapper">
              <table class="data-table">
                <thead>
                  <tr><th>Version</th><th>First seen</th><th>Last seen</th></tr>
                </thead>
                <tbody>
                  <tr v-for="v in data.versions" :key="v.version">
                    <td><span class="badge version">{{ v.version }}</span></td>
                    <td>{{ formatTimestamp(v.first_seen) }}</td>
                    <td>{{ formatTimestamp(v.last_seen) }}</td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>

          <div class="chart-section">
            <h3>Timeline</h3>
            <ChartCaption text="Every event this device reported in the last 90 days, newest first" />
            <div v-if="data.timeline.length === 0" class="empty-state">No events</div>
            <div v-else class="table-wrapper event-log">
              <table class="data-table">
                <thead>
                  <tr><th>Time</th><th>Kind</th><th>Fields</th></tr>
                </thead>
                <tbody>
                  <tr v-for="(e, i) in data.timeline" :key="i">
                    <td>{{ formatTimestamp(e.timestamp) }}</td>
                    <td><span class="badge">{{ e.kind }}</span></td>
                    <td class="mono">{{ fieldPairs(e.fields) }}</td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>
        </template>
      </DashSection>
    </div>
  </AppLayout>
</template>

<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { useRoute } from 'vue-router'
import { api, ApiError, type DevicePageData } from '@/services/api'
import { formatTimestamp, shortDeviceId, titleCase } from '@/utils/format'
import AppLayout from '@/components/AppLayout.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import DashSection from '@/components/DashSection.vue'

type Entries = Record<string, string | number>

const route = useRoute()
const id = computed(() => String(route.params.id ?? ''))
const data = ref<DevicePageData | null>(null)
const loading = ref(true)
const error = ref('')
const missing = ref(false)
let latest = 0

// The worker answers 400 for a malformed id and 404 for one with no rows in the
// retention window; both mean there is nothing to show.
const isNotFound = (e: unknown) => e instanceof ApiError && (e.status === 400 || e.status === 404)

watch(id, async (deviceId) => {
  const ticket = ++latest
  loading.value = true
  error.value = ''
  missing.value = false
  data.value = null
  try {
    const result = await api.getDevice(deviceId)
    if (ticket === latest) data.value = result
  } catch (e) {
    if (ticket !== latest) return
    if (isNotFound(e)) missing.value = true
    else error.value = e instanceof Error ? e.message : 'Failed to load data'
  } finally {
    if (ticket === latest) loading.value = false
  }
}, { immediate: true })

function isEmpty(entries: Entries | null): boolean {
  return !entries || Object.keys(entries).length === 0
}

function fieldPairs(fields: Entries): string {
  return Object.entries(fields).map(([k, v]) => `${k}=${v}`).join(' ')
}
</script>

<style scoped>
.device-id {
  font-size: 12px;
  color: var(--text-secondary);
  word-break: break-all;
}
</style>
