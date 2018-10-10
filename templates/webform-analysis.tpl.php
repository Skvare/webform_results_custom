<?php
/**
 * @file
 * Template for printing out the contents of the "Analysis" tab on a Webform.
 *
 * Available variables:
 * - $node: The node object for this webform.
 * - $component: If a single components analysis is being printed, this will
 *   contain a Webform component. Otherwise all components are having their
 *   analysis printed on the same page.
 * - $analysis: A renderable object containing the following children:
 *   - 'exposed_filter': The output of any exposed filter created by the
 *     webform_analysis, webform_analysis_CONTENTTYPE, or webform_analysis_NID
 *     view.
 *   - 'form': A form for selecting which components should be included in the
 *     analysis.
 *   - 'data': An render array of analysis results for each component enabled.
 */
?>
<div class="webform-analysis">
  <h2 class="analysis-title"><?php print $node->title; ?></h2>
  <?php print drupal_render($analysis['form']['help']); ?>
  <?php if (!empty($analysis['exposed_filter'])): ?>
    <?php print drupal_render($analysis['exposed_filter']); ?>
  <?php endif; ?>
  <div class="webform-analysis-info-header">
    <?php if (!empty($analysis['date_range'])): ?>
      <div class="date-range"><strong>Report date range: </strong><?php print $analysis['date_range']?></div>
    <?php endif; ?>
      <div class="submission-count"><strong>Number of completed evaluations: </strong><?php print $analysis['submission_count']?></div>
    <?php if (isset($analysis['course_rating_average'])): ?>
        <div class="average-rating"><strong>Overall Satisfaction Rating: </strong><?php print $analysis['course_rating_average']?></div>
    <?php endif; ?>
  </div>
  <div class="webform-analysis-data">
    <?php print drupal_render($analysis['data']); ?>
  </div>
  <?php if (!empty($analysis['form'])): ?>
    <?php print drupal_render($analysis['form']); ?>
  <?php endif; ?>
  <?php /* Print out any remaining part of the renderable. */ ?>
  <?php //print drupal_render_children($analysis); ?>
</div>
