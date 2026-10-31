<template>
  <DashSection title="Success by printer model" :loading="loading" :error="error">
    <template v-if="data">
      <ChartCaption :text="`Sample-weighted print outcomes per printer model, ${span}; spellings of one model are merged; click a bar to filter`" />
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
import { canonicalModels } from '@/utils/models'
import { barChartHeight } from '@/utils/series'

const { data, loading, error, span, filters } = useDashData(api.getPrints)
const router = useRouter()

// Canonical models, each recombined from the outcome counts of its raw spellings.
const rows = computed((): SuccessRow[] => {
  const byModel = new Map((data.value?.by_model ?? []).map((m) => [m.model, m]))
  return canonicalModels((data.value?.by_model ?? []).map((m) => ({ name: m.model, count: m.total }))).map((g) => {
    const successes = g.members.reduce((sum, m) => sum + (byModel.get(m)?.successes ?? 0), 0)
    return { ...g, rate: g.count > 0 ? successes / g.count : null }
  })
})

const chartData = computed(() => ({
  labels: rows.value.map((r) => `${r.label} (${r.count.toLocaleString()})`),
  datasets: [{ label: 'Success rate', data: rows.value.map((r) => r.rate), backgroundColor: '#10b981' }]
}))

function drillRow(row: SuccessRow | undefined) {
  if (row) filters.activate(router, [['model', row]])
}
</script>
