<template>
  <AppLayout>
    <div class="page">
      <div class="page-header">
        <h2>Devices</h2>
      </div>

      <DashSection title="Devices" :loading="loading" :error="error">
        <template v-if="data">
          <ChartCaption :text="caption" />
          <NoData v-if="data.devices.length === 0" />
          <div v-else class="table-wrapper">
            <table class="data-table">
              <thead>
                <tr>
                  <th>Device</th>
                  <th>Last seen</th>
                  <th>Version</th>
                  <th>Platform</th>
                  <th>Board</th>
                  <th>Model</th>
                  <th>Sessions</th>
                  <th>Crashes</th>
                  <th>Prints</th>
                </tr>
              </thead>
              <tbody>
                <tr
                  v-for="d in data.devices"
                  :key="d.device_id"
                  class="clickable-row"
                  @click="open(d.device_id)"
                >
                  <td class="mono">{{ shortDeviceId(d.device_id) }}</td>
                  <td>{{ formatTimestamp(d.last_seen) }}</td>
                  <td><span class="badge version">{{ d.version || '-' }}</span></td>
                  <td>{{ d.platform || '-' }}</td>
                  <td>{{ d.board || '-' }}</td>
                  <td>{{ d.model || '-' }}</td>
                  <td class="mono">{{ d.sessions.toLocaleString() }}</td>
                  <td class="mono" :title="countTitle('crashes')">{{ count(d.crashes, 'crashes') }}</td>
                  <td class="mono" :title="countTitle('prints')">{{ count(d.prints, 'prints') }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </template>
      </DashSection>
    </div>
  </AppLayout>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRouter } from 'vue-router'
import { api } from '@/services/api'
import type { DeviceListData } from '@/services/api'
import { useDashData } from '@/composables/useDashData'
import { formatTimestamp, notReportedPer, shortDeviceId } from '@/utils/format'
import AppLayout from '@/components/AppLayout.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import DashSection from '@/components/DashSection.vue'
import NoData from '@/components/NoData.vue'

const LIST_LIMIT = 200

type CountColumn = keyof DeviceListData['not_applicable']

const router = useRouter()
const { data, loading, error, span, filters } = useDashData(api.getDevices)

const caption = computed(() => {
  const total = data.value?.total ?? 0
  const head = total > LIST_LIMIT
    ? `Showing ${LIST_LIMIT} of ${total.toLocaleString()} devices`
    : `${total.toLocaleString()} devices`
  return `${head}, active in the ${span.value}; click a row for its timeline`
})

// A count under a filter its kind does not carry is unknown, not zero.
function count(value: number, column: CountColumn): string {
  return data.value?.not_applicable[column] ? 'n/a' : value.toLocaleString()
}

function countTitle(column: CountColumn): string | undefined {
  const key = data.value?.not_applicable[column]
  return key ? notReportedPer(key) : undefined
}

function open(id: string) {
  router.push({ path: `/device/${id}`, query: filters.queryParams })
}
</script>
