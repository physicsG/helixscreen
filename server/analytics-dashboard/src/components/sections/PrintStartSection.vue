<template>
  <DashSection title="Print start" :loading="loading" :error="error">
    <template v-if="data">
      <NoData v-if="!start" />
      <template v-else>
        <ChartCaption :text="`Sample-weighted print starts, ${span}`" />
        <div class="metrics-row">
          <MetricCard
            title="Thumbnail Adoption"
            :value="formatRate(start.thumbnail_rate / 100)"
            subtitle="starts whose file has a thumbnail"
            color="var(--accent-green)"
          />
          <MetricCard
            title="AMS Usage"
            :value="formatRate(start.ams_rate / 100)"
            subtitle="starts with a filament system active"
            color="var(--accent-blue)"
          />
        </div>

        <div class="grid-2col">
          <div class="chart-section">
            <h3>Slicer Distribution</h3>
            <ChartCaption :text="`Sample-weighted print starts per slicer, ${span}`" />
            <NoData v-if="start.slicers.length === 0" />
            <PieChart v-else :data="slicerChartData" />
          </div>
          <div class="chart-section">
            <h3>Source Distribution</h3>
            <ChartCaption :text="`Sample-weighted print starts per source, ${span}`" />
            <NoData v-if="start.sources.length === 0" />
            <PieChart v-else :data="sourceChartData" />
          </div>
        </div>

        <div class="chart-section">
          <h3>File Size Buckets</h3>
          <ChartCaption :text="`Sample-weighted print starts per file size, ${span}`" />
          <NoData v-if="start.file_size_buckets.length === 0" />
          <BarChart v-else :data="fileSizeChartData" />
        </div>
      </template>
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
import { formatRate } from '@/utils/format'
import { seriesColor } from '@/utils/series'

const { data, loading, error, span } = useDashData(api.getPrints)

const start = computed(() => data.value?.start_context ?? null)

const slicerChartData = computed(() => ({
  labels: start.value?.slicers.map(s => s.name) ?? [],
  datasets: [{
    data: start.value?.slicers.map(s => s.count) ?? [],
    backgroundColor: start.value?.slicers.map((_, i) => seriesColor(i)) ?? []
  }]
}))

const sourceChartData = computed(() => ({
  labels: start.value?.sources.map(s => s.name) ?? [],
  datasets: [{
    data: start.value?.sources.map(s => s.count) ?? [],
    backgroundColor: start.value?.sources.map((_, i) => seriesColor(i)) ?? []
  }]
}))

const fileSizeChartData = computed(() => ({
  labels: start.value?.file_size_buckets.map(b => b.name) ?? [],
  datasets: [{
    label: 'Files',
    data: start.value?.file_size_buckets.map(b => b.count) ?? [],
    backgroundColor: '#f59e0b'
  }]
}))
</script>
