<template>
  <p class="chart-caption">{{ text }}</p>
</template>

<script setup lang="ts">
defineProps<{
  text: string
}>()
</script>

<style scoped>
.chart-caption {
  margin: -6px 0 10px;
  font-size: 12px;
  color: var(--text-secondary);
  opacity: 0.8;
}
</style>
