<template>
  <AppLayout>
    <div class="page">
      <div class="page-header">
        <h2>Usage</h2>
      </div>

      <PanelsSection />
      <FeaturesSection />
      <SettingsSection />
      <ThemesLocalesSection />
    </div>
  </AppLayout>
</template>

<script setup lang="ts">
import AppLayout from '@/components/AppLayout.vue'
import PanelsSection from '@/components/sections/PanelsSection.vue'
import FeaturesSection from '@/components/sections/FeaturesSection.vue'
import SettingsSection from '@/components/sections/SettingsSection.vue'
import ThemesLocalesSection from '@/components/sections/ThemesLocalesSection.vue'
</script>
