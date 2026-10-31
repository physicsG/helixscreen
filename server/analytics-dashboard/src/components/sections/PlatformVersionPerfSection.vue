<template>
  <DashSection title="Per platform and version" :loading="loading" :error="error">
    <template v-if="data">
      <ChartCaption :text="`Sample-weighted average of each device's p95 frame time and RSS, ${span}; click a platform or version to filter`" />
      <NoData v-if="rows.length === 0" />
      <div v-else class="table-wrapper">
        <table class="data-table">
          <thead>
            <tr>
              <th>Platform</th>
              <th>Version</th>
              <th>Devices</th>
              <th>p95 frame</th>
              <th>Avg RSS</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="r in rows" :key="`${r.platform}|${r.version}`">
              <td><button class="cell-link" :title="rowHint(single(r.platform))" @click="drill('platform', r.platform)">{{ r.platform }}</button></td>
              <td><button class="cell-link badge version" :title="rowHint(single(r.version))" @click="drill('version', r.version)">{{ r.version }}</button></td>
              <td class="mono">{{ r.devices.toLocaleString() }}</td>
              <td class="mono">{{ r.p95_frame_ms.toFixed(1) }} ms</td>
              <td class="mono">{{ r.avg_rss_kb === null ? 'n/a' : formatMB(r.avg_rss_kb) }}</td>
            </tr>
          </tbody>
        </table>
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRouter } from 'vue-router'
import DashSection from '@/components/DashSection.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { compareVersions } from '@/utils/chart'
import { formatMB } from '@/utils/format'
import { rowHint, single } from '@/utils/drill'
import type { DashFilterKey } from '@/utils/urlState'

const { data, loading, error, span, filters } = useDashData(api.getPerformance)
const router = useRouter()

// Platforms alphabetically, newest version first within each.
const rows = computed(() =>
  [...(data.value?.by_platform_version ?? [])].sort(
    (a, b) => a.platform.localeCompare(b.platform) || compareVersions(b.version, a.version)
  )
)

function drill(key: DashFilterKey, value: string) {
  filters.activate(router, [[key, single(value)]])
}
</script>

<style scoped>
.cell-link {
  background: none;
  border: none;
  color: inherit;
  font: inherit;
  padding: 0;
  cursor: pointer;
}

.cell-link:hover {
  text-decoration: underline;
}

.cell-link.badge {
  padding: 2px 8px;
}
</style>
