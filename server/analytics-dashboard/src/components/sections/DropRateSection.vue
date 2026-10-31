<template>
  <DashSection title="Dropped frames" :loading="loading" :error="error">
    <template v-if="data">
      <div class="metrics-row">
        <MetricCard
          title="Fleet Drop Rate"
          :value="formatRate(data.fleet_drop_rate, 2)"
          :subtitle="`dropped over rendered frames, ${span}`"
          color="var(--accent-yellow)"
        />
        <MetricCard
          title="High Drop Devices (>5%)"
          :value="`${data.high_drop_devices} / ${data.total_devices}`"
          color="var(--accent-red)"
        />
      </div>

      <div class="chart-section">
        <h3>Drop Rate by Platform</h3>
        <ChartCaption :text="`Sample-weighted dropped over rendered frames per platform, ${span}; click a bar to filter`" />
        <NoData v-if="platforms.length === 0" />
        <BarChart
          v-else
          :data="dropByPlatformData"
          :options="dropOpts"
          :height="barChartHeight(platforms.length)"
          @select="(i) => drillPlatform(platforms[i]?.platform)"
        />
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useRouter } from 'vue-router'
import type { ChartOptions } from 'chart.js'
import DashSection from '@/components/DashSection.vue'
import BarChart from '@/components/BarChart.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { rateAxis } from '@/utils/chart'
import { formatRate } from '@/utils/format'
import { rowHint, single } from '@/utils/drill'
import { barChartHeight } from '@/utils/series'

const { data, loading, error, span, filters } = useDashData(api.getPerformance)
const router = useRouter()

const platforms = computed(() => data.value?.drop_rate_by_platform ?? [])

function drillPlatform(platform: string | undefined) {
  if (platform !== undefined) filters.activate(router, [['platform', single(platform)]])
}

const dropOpts = computed((): ChartOptions<'bar'> => ({
  indexAxis: 'y',
  scales: { x: rateAxis(1), y: { ticks: { autoSkip: false } } },
  plugins: {
    legend: { display: false },
    tooltip: {
      callbacks: {
        label: (ctx) => formatRate(Number(ctx.raw), 2),
        afterLabel: (ctx) => rowHint(single(platforms.value[ctx.dataIndex]?.platform ?? ''))
      }
    }
  }
}))

const dropByPlatformData = computed(() => ({
  labels: platforms.value.map(p => p.platform),
  datasets: [{
    label: 'Drop Rate',
    data: platforms.value.map(p => p.rate),
    backgroundColor: '#ef4444',
  }],
}))
</script>
