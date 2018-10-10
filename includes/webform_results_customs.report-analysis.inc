<?php

module_load_include('inc', 'webform', 'includes/webform.report');

/**
 * Provides a simple analysis of all submissions to a webform.
 *
 * Copied from the original, to be customized, see webform/includes/webform.report.inc
 *
 * @param $node
 *   The webform node on which to generate the analysis.
 * @param $sids
 *   An array of submission IDs to which this analysis may be filtered. May be
 *   used to generate results that are per-user or other groups of submissions.
 * @param $analysis_component
 *   A webform component. If passed in, additional information may be returned
 *   relating specifically to that component's analysis, such as a list of
 *   "Other" values within a select list.
 *
 * @return string
 */
function webform_results_custom_analysis($node, $sids = array(), $analysis_component = NULL) {
  if (!is_array($sids)) {
    $sids = array();
  }

  // Build a renderable for the content of this page.
  $analysis = array(
    '#theme' => array('webform_analysis__' . $node->nid, 'webform_analysis'),
    '#node' => $node,
    '#component' => $analysis_component,
  );

  // See if a query (possibly with exposed filter) needs to restrict the
  // submissions that are being analyzed.
  $query = NULL;
  if (empty($sids)) {
    $view = webform_get_view($node, 'webform_analysis');
    if ($view->type != t('Default') || $view->name != 'webform_analysis') {
      // The view has been customized from the no-op built-in view. Use it.
      $view->set_display();
      $view->init_handlers();
      $view->override_url = $_GET['q'];
      $view->preview = TRUE;
      $view->pre_execute(array($node->nid));
      $view->build();
      // Let modules modify the view just prior to executing it.
      foreach (module_implements('views_pre_execute') as $module) {
        $function = $module . '_views_pre_execute';
        $function($view);
      }
      // If the view is already executed, there was an error in generating it.
      $query = $view->executed ? NULL : $view->query->query();

      // get total submissions count returned by view, and place in variable to be put on page.
      $view->execute();
      $view->post_execute();

      $analysis['submission_count'] = !empty($view->result) ? count($view->result) : 0;
      if (isset($view->exposed_widgets)) {
        $analysis['exposed_filter']['#markup'] = $view->exposed_widgets;
      }
      // custom additions, grab the date filter for placing date range on page
      if (!empty($view->exposed_raw_input['completed']['value'])) {
        $analysis['start_date'] = $view->exposed_raw_input['completed']['value'];
      }
      if (!empty($view->exposed_raw_input['completed_1']['value'])) {
        $analysis['end_date'] = $view->exposed_raw_input['completed_1']['value'];
      }

    }
  }

  // If showing all components, display selection form.
  if (!$analysis_component) {
    // use copied and modified version of analysis components form
    $analysis['form'] = drupal_get_form('webform_analysis_components_form', $node);
  }

  // Add all the components to the analysis renderable array.
  $components = isset($analysis_component) ? array($analysis_component['cid']) : webform_analysis_enabled_components($node);
  $url_components = drupal_parse_url(request_uri());
  $url_params = !empty($url_components['query']) ? $url_components['query'] : [];
  foreach ($components as $cid) {
    // Do component specific call.
    $component = $node->webform['components'][$cid];
    if ($data = webform_results_customs_webform_component_invoke($component['type'], 'analysis', $component, $sids, isset($analysis_component), $query, $url_params)) {
      if (!empty($url_components['query']['percent'])) {
        $data['percent'] = TRUE;
      }
      drupal_alter('webform_analysis_component_data', $data, $node, $component);
      $analysis['data'][$cid] = array(
        '#theme' => array('webform_analysis_component__' . $node->nid . '__' . $cid, 'webform_analysis_component__' . $node->nid, 'webform_analysis_component'),
        '#node' => $node,
        '#component' => $component,
        '#data' => $data,
      );
      $analysis['data'][$cid]['basic'] = array(
        '#theme' => array('webform_analysis_component_basic__' . $node->nid . '__' . $cid, 'webform_analysis_component_basic__' . $node->nid, 'webform_analysis_component_basic'),
        '#component' => $component,
        '#data' => $data,
      );
    }
  }

  drupal_alter('webform_analysis', $analysis);
  return drupal_render($analysis);
}

/**
 * Invoke a component callback.
 *
 * @param $type
 *   The component type as a string.
 * @param $callback
 *   The callback to execute.
 * @param ...
 *   Any additional parameters required by the $callback.
 *
 * @return mixed
 */
function webform_results_customs_webform_component_invoke($type, $callback) {
  $args = func_get_args();
  $type = array_shift($args);
  $callback = array_shift($args);
  $function = '_webform_' . $callback . '_' . $type;
  webform_component_include($type);
  $modified_elements = [
    'select',
    'grid',
    'textfield',
    'number',
    'textarea',
    'email',
    'date',
  ];
  if (in_array($type, $modified_elements)) {
    $function = '_webform_results_customs_analysis_' . $type;
  }
  if (function_exists($function)) {
    return call_user_func_array($function, $args);
  }
}

function _webform_results_customs_get_query_string(SelectQueryInterface $query) {
  $string = (string) $query;
  $arguments = $query->arguments();

  if (!empty($arguments) && is_array($arguments)) {
    foreach ($arguments as $placeholder => &$value) {
      if (is_string($value)) {
        $value = "'$value'";
      }
    }

    $string = strtr($string, $arguments);
  }

  return $string;
}

/**
 * Implements _webform_analysis_component().
 */
function _webform_results_customs_analysis_select($component, $sids = array(), $single = FALSE, $join = NULL, $url_params = []) {
  $options = _webform_select_options($component, TRUE);

  // Create a generic query for the component.
  $query = db_select('webform_submitted_data', 'wsd', array('fetch' => PDO::FETCH_ASSOC))
    ->condition('wsd.nid', $component['nid'])
    ->condition('wsd.cid', $component['cid'])
    ->condition('wsd.data', '', '<>');

  if ($sids) {
    $query->condition('wsd.sid', $sids, 'IN');
  }
  if ($join) {
    $query->innerJoin($join, 'ws2_', 'wsd.sid = ws2_.sid');
  }

  // Clone the query for later use, if needed.
  if ($component['extra']['other_option']) {
    $count_query = clone $query;
    if ($single) {
      $other_query = clone $query;
    }
  }

  $rows = array();
  $other = array();
  $normal_count = 0;

  if ($options) {
    // Gather the normal results first (not "other" options).
    $query->addExpression('COUNT(wsd.data)', 'datacount');

    $query->condition('wsd.data', array_keys($options), 'IN')
      ->fields('wsd', array('data'))
      ->groupBy('wsd.data');
    $result = $query->execute();

    foreach ($result as $data) {
      $display_option = isset($options[$data['data']]) ? $options[$data['data']] : $data['data'];
      $rows[$data['data']] = array(webform_filter_xss($display_option), $data['datacount']);
      $normal_count += $data['datacount'];
    }

    // Order the results according to the normal options array.
    $ordered_rows = array();
    foreach (array_intersect_key($options, $rows) as $key => $label) {
      $ordered_rows[] = $rows[$key];
    }
    $rows = $ordered_rows;
  }

  // Add a row for displaying the total unknown or user-entered values.
  if ($component['extra']['other_option']) {
    $count_query->addExpression('COUNT(*)', 'datacount');
    $full_count = $count_query->execute()->fetchField();
    $other_count = $full_count - $normal_count;
    $display_option = !empty($component['extra']['other_text']) ? check_plain($component['extra']['other_text']) : t('Other...');
    $other_text = ($other_count && !$single) ? $other_count . ' (' . l(t('view'), 'node/' . $component['nid'] . '/webform-results/analysis/' . $component['cid']) . ')' : $other_count;
    $rows[] = array($display_option, $other_text);

    // If showing all results, execute the "other" query and append their rows.
    if ($single) {
      $other_query->addExpression('COUNT(wsd.data)', 'datacount');
      $other_query
        ->fields('wsd', array('data'))
        ->groupBy('wsd.data');
      if ($options) {
        $other_query->condition('wsd.data', array_keys($options), 'NOT IN');
      }
      $other_result = $other_query->execute();
      foreach ($other_result as $data) {
        $other[] = array(check_plain($data['data']), $data['datacount']);
      }
      if ($other) {
        array_unshift($other, '<strong>' . t('Other responses') . '</strong>');
      }
    }
  }

  return array(
    'table_rows' => $rows,
    'other_data' => $other,
  );
}

/**
 * Implements _webform_analysis_component().
 */
function _webform_results_customs_analysis_textfield($component, $sids = array(), $single = FALSE, $join = NULL, $url_params = []) {
  $query = db_select('webform_submitted_data', 'wsd', array('fetch' => PDO::FETCH_ASSOC))
    ->fields('wsd', array('data'))
    ->condition('wsd.nid', $component['nid'])
    ->condition('wsd.cid', $component['cid']);

  if (count($sids)) {
    $query->condition('wsd.sid', $sids, 'IN');
  }

  if ($join) {
    $query->innerJoin($join, 'ws2_', 'wsd.sid = ws2_.sid');
  }

  $nonblanks = 0;
  $submissions = 0;
  $wordcount = 0;

  $submitted_answers = [];

  $result = $query->execute();
  foreach ($result as $data) {
    if (drupal_strlen(trim($data['data'])) > 0) {
      $nonblanks++;
      $wordcount += str_word_count(trim($data['data']));
      if (drupal_strlen(trim($data['data'])) > 4) {
        $submitted_answers[] = $data['data'];
      }
    }
    $submissions++;
  }

  $rows[0] = array(t('Left Blank'), ($submissions - $nonblanks));
  $rows[1] = array(t('User entered value'), $nonblanks);

  $other[] = array(t('Average submission length in words (ex blanks)'), ($nonblanks != 0 ? number_format($wordcount/$nonblanks, 2) : '0'));

  return array(
    'table_rows' => $rows,
    'other_data' => $other,
    'submitted_answers' => $submitted_answers,
  );
}

/**
 * Implements _webform_analysis_component().
 */
function _webform_results_customs_analysis_grid($component, $sids = array(), $single = FALSE, $join = NULL, $url_params = []) {
  // Generate the list of options and questions.
  $node = node_load($component['nid']);
  $questions = _webform_select_options_from_text($component['extra']['questions'], TRUE);
  $questions = _webform_select_replace_tokens($questions, $node);
  $options = _webform_select_options_from_text($component['extra']['options'], TRUE);
  $options = _webform_select_replace_tokens($options, $node);

  // Generate a lookup table of results.
  $query = db_select('webform_submitted_data', 'wsd')
    ->fields('wsd', array('no', 'data'))
    ->condition('wsd.nid', $component['nid'])
    ->condition('wsd.cid', $component['cid'])
    ->condition('wsd.data', '', '<>');

  $query->groupBy('wsd.no')
    ->groupBy('wsd.data');
  $query->addExpression('COUNT(wsd.sid)', 'datacount');


  if (count($sids)) {
    $query->condition('wsd.sid', $sids, 'IN');
  }

  if ($join) {
    $query->innerJoin($join, 'ws2_', 'wsd.sid = ws2_.sid');
  }

  $result = $query->execute();
  $counts = array();
  foreach ($result as $data) {
    $counts[$data->no][$data->data] = $data->datacount;
  }

  // Create an entire table to be put into the returned row.
  $rows = array();
  $header = array('');

  // Add options as a header row.
  foreach ($options as $option) {
    $header[] = webform_filter_xss($option);
  }

  // Add questions as each row.
  foreach ($questions as $qkey => $question) {
    $row = array(webform_filter_xss($question));
    foreach ($options as $okey => $option) {
      $row[] = !empty($counts[$qkey][$okey]) ? $counts[$qkey][$okey] : 0;
    }
    $rows[] = $row;
  }

  // Return return the table unless there are no internal questions in the grid.
  if ($rows) {
    return array(
      'table_header' => $header,
      'table_rows' => $rows,
    );
  }
}

/**
 * Implements _webform_analysis_component().
 */
function _webform_results_customs_analysis_textarea($component, $sids = array(), $single = FALSE, $join = NULL, $url_params = []) {
  $query = db_select('webform_submitted_data', 'wsd', array('fetch' => PDO::FETCH_ASSOC))
    ->fields('wsd', array('no', 'data'))
    ->condition('wsd.nid', $component['nid'])
    ->condition('wsd.cid', $component['cid']);

  if (count($sids)) {
    $query->condition('wsd.sid', $sids, 'IN');
  }

  if ($join) {
    $query->innerJoin($join, 'ws2_', 'wsd.sid = ws2_.sid');
  }

  $nonblanks = 0;
  $submissions = 0;
  $wordcount = 0;

  $submitted_answers = [];

  $result = $query->execute();
  foreach ($result as $data) {
    if (drupal_strlen(trim($data['data'])) > 0) {
      $nonblanks++;
      $wordcount += str_word_count(trim($data['data']));
      if (drupal_strlen(trim($data['data'])) > 4) {
        $submitted_answers[] = $data['data'];
      }
    }
    $submissions++;
  }

  $rows[0] = array(t('Left Blank'), ($submissions - $nonblanks));
  $rows[1] = array(t('User entered value'), $nonblanks);

  $other[] = array(t('Average submission length in words (ex blanks)'), ($nonblanks != 0 ? number_format($wordcount/$nonblanks, 2) : '0'));

  return array(
    'table_rows' => $rows,
    'other_data' => $other,
    'submitted_answers' => $submitted_answers,
  );
}

/**
 * Implements _webform_analysis_component().
 */
function _webform_results_customs_analysis_number($component, $sids = array(), $single = FALSE, $join = NULL, $url_params = []) {
  $advanced_stats = $single;

  $query = db_select('webform_submitted_data', 'wsd', array('fetch' => PDO::FETCH_ASSOC))
    ->fields('wsd', array('data'))
    ->condition('wsd.nid', $component['nid'])
    ->condition('wsd.cid', $component['cid']);

  if (count($sids)) {
    $query->condition('wsd.sid', $sids, 'IN');
  }

  if ($join) {
    $query->innerJoin($join, 'ws2_', 'wsd.sid = ws2_.sid');
  }

  $population = array();
  $submissions = 0;
  $non_zero = 0;
  $non_empty = 0;
  $sum = 0;

  $result = $query->execute();
  foreach ($result as $data) {
    $value = trim($data['data']);
    $number = (float)$value;
    $non_empty += (integer)($value !== '');
    $non_zero += (integer)($number != 0.0);
    $sum += $number;
    $population[] = $number;
    $submissions++;
  }
  sort($population, SORT_NUMERIC);

  // Average and population count.
  if ($component['extra']['excludezero']) {
    $average = $non_zero ? ($sum / $non_zero) : 0;
    $average_title = t('Average !mu excluding zeros/blanks', array('!mu' => $advanced_stats ? '(&mu;)' : ''));
    // Sample (sub-set of total population).
    $population_count = $non_zero - 1;
    $sigma = 'sd';
    $description = t('sample');
  }
  else {
    $average = $submissions ? ($sum / $submissions) : 0;
    $average_title = t('Average !mu including zeros/blanks', array('!mu' => $advanced_stats ? '(&mu;)' : ''));
    // Population.
    $population_count = $submissions;
    $sigma = '&sigma;';
    $description = t('population');
  }

  // Formatting.
  $average = _webform_number_format($component, $average);
  $sum = _webform_number_format($component, $sum);

  $rows[] = array(t('Zero/blank'), ($submissions - $non_zero));
  $rows[] = array(t('User entered value'), $non_empty);

  $other[] = array(t('Sum') . ($advanced_stats ? ' (&Sigma;)' : ''), $sum);
  $other[] = array($average_title, $average);

  if (!$advanced_stats && $sum != 0) {
    $other[] = l(t('More stats »'), 'node/' . $component['nid'] . '/webform-results/analysis/' . $component['cid']);
  }

  // Normal distribution information.
  if ($advanced_stats && $population_count && $sum != 0) {
    // Standard deviation.
    $stddev = 0;
    foreach ($population as $value) {
      // Obtain the total of squared variances.
      $stddev += pow(($value - $average), 2);
    }
    if ($population_count > 0) {
      $stddev = sqrt($stddev / $population_count);
    }
    else {
      $stddev = sqrt($stddev);
    }

    // Skip the rest of the distribution rows if standard deviation is 0.
    if (empty($stddev)) {
      return array(
        'table_rows' => $rows,
        'other_data' => $other,
      );
    }

    // Build normal distribution table rows.
    $count = array();
    $percent = array();
    $limit = array();
    $index = 0;

    $count[] = 0;
    $limit[] = $average - ($stddev * 4);
    foreach ($population as $value) {
      while ($value >= $limit[$index]) {
        $percent[] = number_format($count[$index] / $population_count * 100, 2, '.', '');
        $limit[] = $limit[$index] + $stddev;
        $index += 1;
        if ($limit[$index] == $average) {
          $limit[$index] = $limit[$index] + $stddev;
        }
        $count[$index] = 0;
      }
      $count[$index] += 1;
    }
    $percent[] = number_format($count[$index] / $population_count * 100, 2, '.', '');

    // Format normal distribution table output.
    $stddev = _webform_number_format($component, $stddev);
    $low = _webform_number_format($component, $population[0]);
    $high = _webform_number_format($component, end($population));
    foreach ($limit as $key => $value) {
      $limit[$key] = _webform_number_format($component, $value);
    }

    // Column headings (override potential theme uppercase, e.g. Seven in D7).
    $header = array(
      t('Normal Distribution'),
      array('data' => '-4' . $sigma, 'style' => 'text-transform: lowercase;'),
      array('data' => '-3' . $sigma, 'style' => 'text-transform: lowercase;'),
      array('data' => '-2' . $sigma, 'style' => 'text-transform: lowercase;'),
      array('data' => '-1' . $sigma, 'style' => 'text-transform: lowercase;'),
      array('data' => '+1' . $sigma, 'style' => 'text-transform: lowercase;'),
      array('data' => '+2' . $sigma, 'style' => 'text-transform: lowercase;'),
      array('data' => '+3' . $sigma, 'style' => 'text-transform: lowercase;'),
      array('data' => '+4' . $sigma, 'style' => 'text-transform: lowercase;'),
    );

    // Insert row labels.
    array_unshift($limit, t('Boundary'));
    array_unshift($count, t('Count'));
    array_unshift($percent, t('% of !description', array('!description' => $description)));

    $normal_distribution = theme('table', array('header' => $header, 'rows' => array($limit, $count, $percent), 'sticky' => FALSE));

    $other[] = array(t('Range'), t('!low to !high', array('!low' => $low, '!high' => $high)));
    $other[] = array(t('Standard deviation (!sigma)', array('!sigma' => $sigma)), $stddev);
    $other[] = $normal_distribution;
  }

  return array(
    'table_rows' => $rows,
    'other_data' => $other,
  );
}

/**
 * Implements _webform_analysis_component().
 */
function _webform_results_customs_analysis_email($component, $sids = array(), $single = FALSE, $join = NULL, $url_params = []) {
  $query = db_select('webform_submitted_data', 'wsd', array('fetch' => PDO::FETCH_ASSOC))
    ->fields('wsd', array('no', 'data'))
    ->condition('wsd.nid', $component['nid'])
    ->condition('wsd.cid', $component['cid']);

  if (count($sids)) {
    $query->condition('wsd.sid', $sids, 'IN');
  }

  if ($join) {
    $query->innerJoin($join, 'ws2_', 'wsd.sid = ws2_.sid');
  }

  $nonblanks = 0;
  $submissions = 0;
  $wordcount = 0;

  $result = $query->execute();
  foreach ($result as $data) {
    if (drupal_strlen(trim($data['data'])) > 0) {
      $nonblanks++;
      $wordcount += str_word_count(trim($data['data']));
    }
    $submissions++;
  }

  $rows[0] = array(t('Left Blank'), ($submissions - $nonblanks));
  $rows[1] = array(t('User entered value'), $nonblanks);

  $other[0] = array(t('Average submission length in words (ex blanks)'), ($nonblanks != 0 ? number_format($wordcount/$nonblanks, 2) : '0'));

  return array(
    'table_rows' => $rows,
    'other_data' => $other,
  );

}

/**
 * Implements _webform_analysis_component().
 */
function _webform_results_customs_analysis_date($component, $sids = array(), $single = FALSE, $join = NULL, $url_params = []) {
  $query = db_select('webform_submitted_data', 'wsd', array('fetch' => PDO::FETCH_ASSOC))
    ->fields('wsd', array('no', 'data'))
    ->condition('wsd.nid', $component['nid'])
    ->condition('wsd.cid', $component['cid']);
  $query->orderBy('wsd.sid');

  $query->orderBy('wsd.sid');

  if (count($sids)) {
    $query->condition('wsd.sid', $sids, 'IN');
  }

  if ($join) {
    $query->innerJoin($join, 'ws2_', 'wsd.sid = ws2_.sid');
  }

  $result = $query->execute();

  $dates = array();
  $submissions = 0;
  foreach ($result as $row) {
    $submissions++;
    if ($row['data']) {
      $dates[] = webform_date_array($row['data']);
    }
  }

  // Display stats.
  $nonblanks = count($dates);
  $rows[0] = array(t('Left Blank'), ($submissions - $nonblanks));
  $rows[1] = array(t('User entered value'), $nonblanks);

  return array(
    'table_rows' => $rows,
  );
}
