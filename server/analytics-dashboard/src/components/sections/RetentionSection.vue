<template>
  <DashSection title="Retention" :loading="loading" :error="error">
    <template v-if="data">
      <ChartCaption
        text="Weekly cohorts by first session, last 90 days: share of each cohort still seen in week N or later; blank weeks have not happened yet"
      />
      <NoData v-if="data.cohorts.length === 0" />
      <div v-else class="table-wrapper">
        <table class="data-table retention">
          <thead>
            <tr>
              <th>Cohort</th>
              <th>Devices</th>
              <th v-for="w in weeks" :key="w">W{{ w }}</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="c in data.cohorts" :key="c.start">
              <td class="mono">{{ c.label }}</td>
              <td class="mono">{{ c.size.toLocaleString() }}</td>
              <td
                v-for="w in weeks"
                :key="w"
                class="mono cell"
                :style="cellStyle(c.survival[w] ?? null)"
              >
                {{ c.survival[w] == null ? '' : formatRate(c.survival[w] ?? null, 0) }}
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </template>
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import DashSection from '@/components/DashSection.vue'
import ChartCaption from '@/components/ChartCaption.vue'
import NoData from '@/components/NoData.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { formatRate } from '@/utils/format'

const { data, loading, error } = useDashData(api.getRetention)

const weeks = computed(() => {
  const n = Math.max(0, ...(data.value?.cohorts ?? []).map((c) => c.survival.length))
  return Array.from({ length: n }, (_, i) => i)
})

function cellStyle(rate: number | null) {
  if (rate === null) return undefined
  return { background: `rgba(59, 130, 246, ${(0.08 + rate * 0.5).toFixed(2)})` }
}
</script>

<style scoped>
.retention td.cell {
  text-align: right;
}
</style>
