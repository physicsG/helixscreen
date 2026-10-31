<template>
  <AppLayout>
    <div class="page">
      <div class="page-header">
        <h2>Stability</h2>
      </div>

      <CrashClustersSection />
      <CrashRateSection />
      <ErrorCodesSection />
      <MemoryWarningsSection />
      <ConnectionSection />
      <DisplayAnomaliesSection />
    </div>
  </AppLayout>
</template>

<script setup lang="ts">
import { nextTick, onMounted, onUnmounted, watch } from 'vue'
import { useRoute } from 'vue-router'
import { useFiltersStore } from '@/stores/filters'
import AppLayout from '@/components/AppLayout.vue'
import CrashClustersSection from '@/components/sections/CrashClustersSection.vue'
import CrashRateSection from '@/components/sections/CrashRateSection.vue'
import ErrorCodesSection from '@/components/sections/ErrorCodesSection.vue'
import MemoryWarningsSection from '@/components/sections/MemoryWarningsSection.vue'
import ConnectionSection from '@/components/sections/ConnectionSection.vue'
import DisplayAnomaliesSection from '@/components/sections/DisplayAnomaliesSection.vue'

const route = useRoute()
const filters = useFiltersStore()

// Sections load their own data, so the hash target appears after the page does.
// Retry until the element exists.
let retry: ReturnType<typeof setTimeout> | undefined
function scrollToHash(attempts = 20) {
  clearTimeout(retry)
  const id = route.hash.slice(1)
  if (!id) return
  const el = document.getElementById(id)
  if (el) el.scrollIntoView({ block: 'center' })
  else if (attempts > 0) retry = setTimeout(() => scrollToHash(attempts - 1), 150)
}

onUnmounted(() => clearTimeout(retry))
onMounted(() => nextTick(() => scrollToHash()))
watch(() => [route.hash, filters.queryString], () => nextTick(() => scrollToHash()))
</script>
