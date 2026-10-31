<template>
  <AppLayout>
    <div class="page">
      <div class="page-header">
        <h2>Releases</h2>
      </div>

      <ReleaseScorecardSection
        :rows="rows"
        :loading="loading"
        :error="error"
        :selected="selected"
        :not-applicable="data?.not_applicable ?? null"
        @toggle="toggleVersion"
        @reset="selected = null"
      />
      <AdoptionCurveSection
        :adoption="data?.adoption ?? []"
        :versions="rows.map((r) => r.version)"
        :loading="loading"
        :error="error"
      />
      <StuckVersionsSection />
    </div>
  </AppLayout>
</template>

<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import AppLayout from '@/components/AppLayout.vue'
import ReleaseScorecardSection from '@/components/sections/ReleaseScorecardSection.vue'
import AdoptionCurveSection from '@/components/sections/AdoptionCurveSection.vue'
import StuckVersionsSection from '@/components/sections/StuckVersionsSection.vue'
import { useFiltersStore } from '@/stores/filters'
import { api } from '@/services/api'
import type { ReleasesData } from '@/services/api'
import { compareVersions } from '@/utils/chart'

const filters = useFiltersStore()
// null: the worker picks the newest versions with traffic in the range.
const selected = ref<string[] | null>(null)
const data = ref<ReleasesData | null>(null)
const loading = ref(true)
const error = ref('')

const rows = computed(() =>
  [...(data.value?.versions ?? [])].sort((a, b) => compareVersions(b.version, a.version))
)

function toggleVersion(v: string) {
  const current = selected.value ?? rows.value.map((r) => r.version)
  const next = current.includes(v) ? current.filter((x) => x !== v) : [...current, v]
  selected.value = next.length > 0 ? next : null
}

let latest = 0
async function fetchReleases() {
  const ticket = ++latest
  loading.value = true
  error.value = ''
  try {
    const result = await api.getReleases(filters.queryString, selected.value ?? undefined)
    if (ticket === latest) data.value = result
  } catch (e) {
    if (ticket === latest) error.value = e instanceof Error ? e.message : 'Failed to load data'
  } finally {
    if (ticket === latest) loading.value = false
  }
}

watch([() => filters.queryString, selected], fetchReleases, { deep: true, immediate: true })
</script>
