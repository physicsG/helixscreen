<template>
  <span v-if="change" class="delta" :class="change.tone">{{ change.text }}</span>
</template>

<script setup lang="ts">
import type { ppChange } from '@/utils/format'

defineProps<{ change: ReturnType<typeof ppChange> }>()
</script>

<style scoped>
.delta {
  display: block;
  font-size: 11px;
  font-weight: 500;
  margin-top: 2px;
}

.good {
  color: var(--accent-green);
}

.bad {
  color: var(--accent-red);
}
</style>
