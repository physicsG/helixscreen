<template>
  <DashSection title="Errors" :loading="loading" :error="error">
    <template v-if="data">
      <div class="metrics-row">
        <MetricCard
          title="Klippy Errors"
          :value="data.delta_era_devices > 0 ? totalKlippyErrors.toLocaleString() : '-'"
          :subtitle="data.delta_era_devices > 0 ? span : deltaEraPlaceholder(data)"
          color="#f97316"
        />
      </div>

      <div class="chart-section">
        <h3>Error Hotspots</h3>
        <ChartCaption :text="`Sample-weighted error reports per category, ${span}`" />
        <NoData v-if="data.error_categories.length === 0" :not-applicable="data.not_applicable.errors" />
        <BarChart
          v-else
          :data="errorCategoryChartData"
          :options="horizontalBarOpts"
          :height="barChartHeight(data.error_categories.length)"
        />
      </div>

      <div class="chart-section">
        <h3>Error Codes</h3>
        <ChartCaption :text="`Sample-weighted error reports per category and code, ${span}`" />
        <NoData v-if="data.error_codes.length === 0" :not-applicable="data.not_applicable.errors" />
        <div v-else class="table-wrapper">
          <table class="data-table">
            <thead>
              <tr>
                <th>Category</th>
                <th>Code</th>
                <th>Count</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="(row, i) in data.error_codes" :key="i" :id="errorAnchor(row.code)">
                <td><span class="badge category">{{ row.category }}</span></td>
                <td class="mono">{{ row.code }}</td>
                <td class="mono">{{ row.count.toLocaleString() }}</td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>

      <div class="chart-section">
        <h3>Error Codes by Version</h3>
        <ChartCaption :text="`Sample-weighted error reports per code and version, ${span}; older lines and unreleased betas are grouped, click a version to expand or filter`" />
        <NoData v-if="codeRows.length === 0" :not-applicable="data.not_applicable.errors" />
        <div v-else class="table-wrapper">
          <table class="data-table matrix">
            <thead>
              <tr>
                <th>Category</th>
                <th>Code</th>
                <th>Total</th>
                <th
                  v-for="col in versionCols"
                  :key="col.key"
                  class="version-col"
                  :title="rowHint(col)"
                  @click="filters.activate(router, [['version', col]])"
                >
                  {{ rowLabel(col) }}
                </th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="row in codeRows" :key="`${row.category}|${row.code}`">
                <td><span class="badge category">{{ row.category }}</span></td>
                <td class="mono">{{ row.code }}</td>
                <td class="mono">{{ row.total.toLocaleString() }}</td>
                <td v-for="col in versionCols" :key="col.key" class="mono cell">
                  {{ cell(row, col) || '' }}
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>

      <div class="chart-section">
        <h3>Klippy Errors &amp; Shutdowns</h3>
        <ChartCaption :text="`Sample-weighted Klippy errors and shutdowns per day from connection snapshots, ${span}; first and current day omitted`" />
        <DeltaEraWaiting v-if="data.delta_era_devices === 0 || data.filter_not_applicable" :era="data" />
        <NoData v-else-if="klippy.length === 0" :era="data" />
        <LineChart v-else :data="klippyChartData" />
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRouter } from 'vue-router'
import DashSection from '@/components/DashSection.vue'
import BarChart from '@/components/BarChart.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import DeltaEraWaiting from '@/components/DeltaEraWaiting.vue'
import LineChart from '@/components/LineChart.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { horizontalBarOpts } from '@/utils/chart'
import { deltaEraPlaceholder } from '@/utils/format'
import { errorAnchor } from '@/utils/anchors'
import { rowHint, rowLabel } from '@/utils/drill'
import { barChartHeight, todayUtc, trimPartialDays } from '@/utils/series'
import { groupVersions, type Grouped } from '@/utils/versions'

const { data, loading, error, span, filters } = useDashData(api.getStability)
const router = useRouter()

// Reports with no version cannot sit in a version column; they still count in Total.
const versionCols = computed(() => {
  const totals = new Map<string, number>()
  for (const r of data.value?.errors_by_version ?? []) {
    if (r.version) totals.set(r.version, (totals.get(r.version) ?? 0) + r.count)
  }
  return groupVersions([...totals].map(([name, count]) => ({ name, count })), new Set(filters.expand))
})

interface CodeRow {
  category: string
  code: string
  total: number
  byVersion: Map<string, number>
}

const codeRows = computed(() => {
  const rows = new Map<string, CodeRow>()
  for (const r of data.value?.errors_by_version ?? []) {
    const key = `${r.category}|${r.code}`
    let row = rows.get(key)
    if (!row) {
      row = { category: r.category, code: r.code, total: 0, byVersion: new Map() }
      rows.set(key, row)
    }
    row.total += r.count
    row.byVersion.set(r.version, (row.byVersion.get(r.version) ?? 0) + r.count)
  }
  return [...rows.values()].sort((a, b) => b.total - a.total)
})

function cell(row: CodeRow, col: Grouped): number {
  return col.members.reduce((sum, m) => sum + (row.byVersion.get(m) ?? 0), 0)
}

const totalKlippyErrors = computed(() =>
  (data.value?.klippy_trend ?? []).reduce((sum, d) => sum + d.errors, 0)
)

const klippy = computed(() => trimPartialDays(data.value?.klippy_trend ?? [], todayUtc()))

const klippyChartData = computed(() => ({
  labels: klippy.value.map(d => d.date),
  datasets: [
    {
      label: 'Errors',
      data: klippy.value.map(d => d.errors),
      borderColor: '#f97316',
      backgroundColor: 'rgba(249, 115, 22, 0.1)',
      fill: false,
      tension: 0.3
    },
    {
      label: 'Shutdowns',
      data: klippy.value.map(d => d.shutdowns),
      borderColor: '#ef4444',
      backgroundColor: 'rgba(239, 68, 68, 0.1)',
      fill: false,
      tension: 0.3
    }
  ]
}))

const errorCategoryChartData = computed(() => ({
  labels: data.value?.error_categories.map(c => c.category) ?? [],
  datasets: [{
    label: 'Count',
    data: data.value?.error_categories.map(c => c.count) ?? [],
    backgroundColor: '#ef4444'
  }]
}))
</script>

<style scoped>
.matrix .version-col {
  cursor: pointer;
}

.matrix .version-col:hover {
  color: var(--text-primary);
}

.matrix td.cell {
  text-align: right;
}
</style>
