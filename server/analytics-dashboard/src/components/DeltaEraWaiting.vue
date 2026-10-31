<template>
  <div class="delta-era-waiting">{{ deltaEraPlaceholder(era) }}</div>
</template>

<script setup lang="ts">
import type { DeltaEra } from '@/services/api'
import { deltaEraPlaceholder } from '@/utils/format'

defineProps<{
  era: DeltaEra
}>()
</script>

<style scoped>
.delta-era-waiting {
  padding: 24px;
  text-align: center;
  color: var(--text-secondary);
  border: 1px dashed var(--border);
  border-radius: 8px;
}
</style>
