<template>
  <div class="empty-state">{{ text }}</div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { DeltaEra } from '@/services/api'
import { useFiltersStore } from '@/stores/filters'
import { DASH_FILTER_KEYS, type DashFilterKey } from '@/utils/urlState'
import { emptyPlaceholder, notReportedPer } from '@/utils/format'

const props = withDefaults(
  defineProps<{
    era?: DeltaEra | null
    /** The active filter this block's rows do not carry, from the endpoint's `not_applicable`. */
    notApplicable?: DashFilterKey | null
  }>(),
  { era: null, notApplicable: null }
)

const filters = useFiltersStore()
const text = computed(() =>
  props.notApplicable
    ? notReportedPer(props.notApplicable)
    : emptyPlaceholder(props.era, DASH_FILTER_KEYS.filter((key) => filters[key].length > 0))
)
</script>
