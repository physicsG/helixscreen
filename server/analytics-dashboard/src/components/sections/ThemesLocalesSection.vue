<template>
  <DashSection title="Themes and locales" :loading="loading" :error="error">
    <template v-if="data">
      <div class="metrics-row">
        <MetricCard
          title="Median Brightness"
          :value="`${Math.round(data.brightness.p50)}%`"
          :subtitle="span"
          color="var(--accent-yellow)"
        />
      </div>

      <div class="grid-2col">
        <div class="chart-section">
          <h3>Theme Distribution</h3>
          <ChartCaption :text="`Distinct devices per custom theme, ${span}`" />
          <NoData v-if="themes.length === 0" />
          <BarChart v-else :data="themeChartData" :options="horizontalBarOpts" :height="barChartHeight(themes.length)" />
        </div>
        <div class="chart-section">
          <h3>Dark vs Light Mode</h3>
          <ChartCaption :text="`Distinct devices per mode, ${span}`" />
          <NoData v-if="data.dark_vs_light.length === 0" />
          <PieChart v-else :data="darkLightChartData" />
        </div>
      </div>

      <div class="grid-2col">
        <div class="chart-section">
          <h3>Locale Breakdown</h3>
          <ChartCaption :text="`Distinct devices per UI language, ${span}`" />
          <NoData v-if="data.locales.length === 0" />
          <PieChart v-else :data="localeChartData" />
        </div>
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import DashSection from '@/components/DashSection.vue'
import BarChart from '@/components/BarChart.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import MetricCard from '@/components/MetricCard.vue'
import NoData from '@/components/NoData.vue'
import PieChart from '@/components/PieChart.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { horizontalBarOpts } from '@/utils/chart'
import { barChartHeight, seriesColor } from '@/utils/series'

const { data, loading, error, span } = useDashData(api.getEngagement)

const themes = computed(() =>
  (data.value?.themes ?? []).filter(t => t.name !== 'dark' && t.name !== 'light')
)

const themeChartData = computed(() => ({
  labels: themes.value.map(t => t.name),
  datasets: [{
    label: 'Users',
    data: themes.value.map(t => t.count),
    backgroundColor: '#8b5cf6'
  }]
}))

const DARK_LIGHT_COLORS: Record<string, string> = { dark: '#1e293b', light: '#f8fafc' }

const darkLightChartData = computed(() => ({
  labels: data.value?.dark_vs_light.map(d => d.name.charAt(0).toUpperCase() + d.name.slice(1)) ?? [],
  datasets: [{
    data: data.value?.dark_vs_light.map(d => d.count) ?? [],
    backgroundColor: data.value?.dark_vs_light.map(d => DARK_LIGHT_COLORS[d.name] ?? '#64748b') ?? [],
    borderColor: '#334155',
    borderWidth: 1
  }]
}))

const localeChartData = computed(() => ({
  labels: data.value?.locales.map(l => l.name) ?? [],
  datasets: [{
    data: data.value?.locales.map(l => l.count) ?? [],
    backgroundColor: data.value?.locales.map((_, i) => seriesColor(i)) ?? []
  }]
}))
</script>
