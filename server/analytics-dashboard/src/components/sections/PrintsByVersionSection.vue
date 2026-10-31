<template>
  <DashSection title="Success by version" :loading="loading" :error="error">
    <template v-if="data">
      <ChartCaption :text="`Sample-weighted print outcomes per HelixScreen version, ${span}; older lines and unreleased betas are grouped, their rate recombined from counts; click a bar to expand or filter`" />
      <NoData v-if="rows.length === 0" :not-applicable="data.not_applicable.prints" />
      <BarChart
        v-else
        :data="chartData"
        :options="successBarOpts(rows)"
        :height="barChartHeight(rows.length)"
        @select="(i) => drillRow(rows[i])"
      />
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRouter } from 'vue-router'
import DashSection from '@/components/DashSection.vue'
import BarChart from '@/components/BarChart.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { successBarOpts, type SuccessRow } from '@/utils/chart'
import { rowLabel } from '@/utils/drill'
import { groupVersionRates } from '@/utils/versions'
import { barChartHeight } from '@/utils/series'

const { data, loading, error, span, filters } = useDashData(api.getPrints)
const router = useRouter()

// Grouped like every version list; a group's rate is recombined from its members' counts.
const rows = computed((): SuccessRow[] =>
  groupVersionRates(
    (data.value?.by_version ?? []).map((v) => ({
      version: v.version,
      numerator: v.successes,
      denominator: v.total,
      rate: v.rate
    })),
    new Set(filters.expand)
  )
)

const chartData = computed(() => ({
  labels: rows.value.map((r) => `${rowLabel(r)} (${r.count.toLocaleString()})`),
  datasets: [{ label: 'Success rate', data: rows.value.map((r) => r.rate), backgroundColor: '#3b82f6' }]
}))

function drillRow(row: SuccessRow | undefined) {
  if (row) filters.activate(router, [['version', row]])
}
</script>
