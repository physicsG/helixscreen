<template>
  <DashSection title="Devices on old versions" :loading="loading" :error="error">
    <DistributionBar
      v-if="data && reference"
      :rows="rows"
      filter-key="version"
      title="Older release lines"
      :caption="`Distinct devices with a session, ${span}, on lines older than the newest two in the fleet; click a line to expand it`"
    />
  </DashSection>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import DashSection from '@/components/DashSection.vue'
import DistributionBar from '@/components/DistributionBar.vue'
import { useDashData } from '@/composables/useDashData'
import { api } from '@/services/api'
import { withoutFilter } from '@/utils/urlState'
import { olderLines } from '@/utils/versions'

const filtered = useDashData(api.getAdoption)
// The newest lines are judged without the version filter, so filtering to an
// old version cannot make its line look current.
const unfiltered = useDashData((qs: string) => api.getAdoption(withoutFilter(qs, 'version')))

const data = filtered.data
const reference = unfiltered.data
const span = filtered.span
const loading = computed(() => filtered.loading.value || unfiltered.loading.value)
const error = computed(() => filtered.error.value || unfiltered.error.value)

const rows = computed(() =>
  olderLines(data.value?.versions ?? [], reference.value?.versions ?? [], new Set(filtered.filters.expand))
)
</script>
