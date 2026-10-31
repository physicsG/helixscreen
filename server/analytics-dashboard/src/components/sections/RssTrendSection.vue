<template>
  <DashSection title="Memory use" :loading="loading" :error="error">
    <template v-if="data">
      <div class="metrics-row">
        <MetricCard
          title="Avg RSS"
          :value="formatMB(avgRss)"
          :subtitle="`resident memory, ${span}`"
          color="var(--accent-blue)"
        />
        <MetricCard
          title="Peak VM"
          :value="formatMB(peakVm)"
          subtitle="virtual memory"
          color="var(--accent-red)"
        />
        <MetricCard
          title="P95 RSS"
          :value="formatMB(p95Rss)"
          subtitle="95th percentile"
          color="var(--accent-yellow)"
        />
      </div>

      <div class="chart-section">
        <h3>RSS Over Time</h3>
        <ChartCaption :text="`Memory snapshots, sample-weighted average and p95 resident memory per day, ${span}; first and current day omitted`" />
        <NoData v-if="rss.length === 0" />
        <LineChart v-else :data="rssChartData" />
      </div>

      <div class="chart-section">
        <h3>Avg RSS by Platform</h3>
        <ChartCaption :text="`Memory snapshots, sample-weighted average resident memory per platform, ${span}`" />
        <NoData v-if="data.rss_by_platform.length === 0" />
        <BarChart
          v-else
          :data="platformChartData"
          :options="horizontalBarOpts"
          :height="barChartHeight(data.rss_by_platform.length)"
        />
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import DashSection from '@/components/DashSection.vue'
import BarChart from '@/components/BarChart.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import LineChart from '@/components/LineChart.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { horizontalBarOpts } from '@/utils/chart'
import { formatMB } from '@/utils/format'
import { barChartHeight, todayUtc, trimPartialDays } from '@/utils/series'

const { data, loading, error, span } = useDashData(api.getMemory)

const avgRss = computed(() => {
  if (!data.value?.rss_over_time.length) return 0
  const sum = data.value.rss_over_time.reduce((a, d) => a + d.avg_rss_kb, 0)
  return sum / data.value.rss_over_time.length
})

const peakVm = computed(() => {
  if (!data.value?.vm_peak_trend.length) return 0
  return Math.max(...data.value.vm_peak_trend.map(d => d.avg_vm_peak_kb))
})

const p95Rss = computed(() => {
  if (!data.value?.rss_over_time.length) return 0
  return Math.max(...data.value.rss_over_time.map(d => d.p95_rss_kb))
})

const rss = computed(() => trimPartialDays(data.value?.rss_over_time ?? [], todayUtc()))

const rssChartData = computed(() => ({
  labels: rss.value.map(d => d.date),
  datasets: [
    {
      label: 'Avg RSS (KB)',
      data: rss.value.map(d => d.avg_rss_kb),
      borderColor: '#3b82f6',
      backgroundColor: 'rgba(59, 130, 246, 0.1)',
      fill: true,
      tension: 0.3
    },
    {
      label: 'P95 RSS (KB)',
      data: rss.value.map(d => d.p95_rss_kb),
      borderColor: '#f59e0b',
      backgroundColor: 'rgba(245, 158, 11, 0.1)',
      fill: false,
      tension: 0.3
    }
  ]
}))

const platformChartData = computed(() => ({
  labels: data.value?.rss_by_platform.map(p => p.platform) ?? [],
  datasets: [{
    label: 'Avg RSS (KB)',
    data: data.value?.rss_by_platform.map(p => p.avg_rss_kb) ?? [],
    backgroundColor: '#3b82f6'
  }]
}))
</script>
